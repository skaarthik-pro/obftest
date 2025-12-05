#!/usr/bin/env python3
import argparse
import queue
import ssl
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Optional, List
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry
from urllib3.poolmanager import PoolManager
import urllib3

# Disable SSL warnings for insecure connections
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


# Rsr represents ?
@dataclass
class Rsr:
    ID: str
    Dest: str


# Rslt represents ?
@dataclass
class Rslt:
    Rsr: Rsr
    IsChkSuccess: bool
    ChkLatency: timedelta
    Error: Optional[Exception]
    Timestamp: datetime


class Stats:
    def __init__(self):
        self.Total = 0
        self.Success = 0
        self.Failures = 0
        self.Errors = 0
        self.TotalLatency = timedelta(0)
        self.mu = threading.Lock()

    def copy(self):
        self.mu.acquire()
        try:
            copy = Stats()
            copy.Total = self.Total
            copy.Success = self.Success
            copy.Failures = self.Failures
            copy.Errors = self.Errors
            copy.TotalLatency = self.TotalLatency
            return copy
        finally:
            self.mu.release()


# Runner performs ?
class Runner:
    def __init__(self, timeout: timedelta, max: int):
        # Create a custom session with SSL verification disabled
        self.session = requests.Session()
        
        # Configure retry strategy
        retry_strategy = Retry(
            total=0,  # No retries
            backoff_factor=0
        )
        
        # Create adapter with custom SSL context
        adapter = HTTPAdapter(
            max_retries=retry_strategy,
            pool_connections=1000,
            pool_maxsize=100,
            pool_block=False
        )
        
        self.session.mount("http://", adapter)
        self.session.mount("https://", adapter)
        
        # Disable SSL verification
        self.session.verify = False
        
        # Set timeout (convert timedelta to seconds)
        self.timeout = timeout.total_seconds()
        self.max = max
        self.rslts = queue.Queue(maxsize=1000)
        self.stats = Stats()

    # Chk performs ?
    def Chk(self, rsr: Rsr) -> Rslt:
        start = time.time()
        rslt = Rslt(
            Rsr=rsr,
            IsChkSuccess=False,
            ChkLatency=timedelta(0),
            Error=None,
            Timestamp=datetime.now()
        )

        try:
            headers = {
                "User-Agent": "Checker/1.0",
                "Connection": "close"
            }
            
            resp = self.session.get(
                rsr.Dest,
                headers=headers,
                timeout=self.timeout,
                stream=True
            )
            
            # Read first 1024 bytes and discard (equivalent to io.CopyN)
            try:
                chunk = resp.raw.read(1024)
            except:
                pass
            
            rslt.ChkLatency = timedelta(seconds=time.time() - start)
            rslt.IsChkSuccess = 200 <= resp.status_code < 400
        except Exception as e:
            rslt.Error = e
            rslt.IsChkSuccess = False
            rslt.ChkLatency = timedelta(seconds=time.time() - start)

        return rslt

    # CheckRsr performs ?
    def CheckRsr(self, rsrs: List[Rsr]):
        sem = threading.Semaphore(self.max)
        
        with ThreadPoolExecutor(max_workers=self.max) as executor:
            futures = []
            
            for rsr in rsrs:
                sem.acquire()
                future = executor.submit(self._check_with_semaphore, sem, rsr)
                futures.append(future)
            
            # Wait for all tasks to complete
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception as e:
                    print(f"Error during checks: {e}")

    def _check_with_semaphore(self, sem: threading.Semaphore, rsr: Rsr):
        try:
            result = self.Chk(rsr)
            try:
                self.rslts.put_nowait(result)
            except queue.Full:
                pass  # Queue is full, skip adding result
            
            self.stats.mu.acquire()
            try:
                self.stats.Total += 1
                if result.IsChkSuccess:
                    self.stats.Success += 1
                else:
                    self.stats.Failures += 1
                if result.Error is not None:
                    self.stats.Errors += 1
                self.stats.TotalLatency += result.ChkLatency
            finally:
                self.stats.mu.release()
        finally:
            sem.release()

    def GetStats(self) -> Stats:
        return self.stats.copy()


def GenerateRsrs(baseURL: str, count: int) -> List[Rsr]:
    rsrs = []
    for i in range(count):
        rsrs.append(Rsr(
            ID=f"rsr-{i+1}",
            Dest=f"{baseURL}/health"
        ))
    return rsrs


def parse_duration(s: str) -> timedelta:
    """Parse duration string like '5s', '10s', etc."""
    if s.endswith('s'):
        seconds = float(s[:-1])
        return timedelta(seconds=seconds)
    elif s.endswith('ms'):
        milliseconds = float(s[:-2])
        return timedelta(milliseconds=milliseconds)
    elif s.endswith('m'):
        minutes = float(s[:-1])
        return timedelta(minutes=minutes)
    else:
        # Try to parse as seconds
        return timedelta(seconds=float(s))


def main():
    parser = argparse.ArgumentParser(description='HTTP health check tool')
    parser.add_argument('-count', type=int, default=100000, help='Number of Rsrs')
    parser.add_argument('-base-url', type=str, default='http://localhost:8080', help='Base URL')
    parser.add_argument('-max', type=int, default=1000, help='Maximum')
    parser.add_argument('-timeout', type=str, default='5s', help='Timeout for each check (e.g., 5s, 10s)')
    parser.add_argument('-report-interval', type=str, default='5s', help='Interval for progress reports (e.g., 5s, 10s)')
    
    args = parser.parse_args()
    
    timeout = parse_duration(args.timeout)
    report_interval = parse_duration(args.report_interval)
    
    rsrs = GenerateRsrs(args.base_url, args.count)
    checker = Runner(timeout, args.max)
    
    done = threading.Event()
    
    def reporter():
        while not done.is_set():
            time.sleep(report_interval.total_seconds())
            if done.is_set():
                return
            stats = checker.GetStats()
            if stats.Total > 0:
                avg_latency = stats.TotalLatency / stats.Total
                print(f"[Progress] Total: {stats.Total} | Success: {stats.Success} | "
                      f"Failures: {stats.Failures} | Errors: {stats.Errors} | "
                      f"Avg Latency: {avg_latency}")
    
    reporter_thread = threading.Thread(target=reporter, daemon=True)
    reporter_thread.start()
    
    start_time = time.time()
    checker.CheckRsr(rsrs)
    done.set()
    
    elapsed = time.time() - start_time
    stats = checker.GetStats()
    
    print("\n=== Final Results ===")
    print(f"Total Rsr checked: {stats.Total}")
    if stats.Total > 0:
        print(f"Success: {stats.Success} ({stats.Success / stats.Total * 100:.2f}%)")
        print(f"Failures: {stats.Failures} ({stats.Failures / stats.Total * 100:.2f}%)")
        print(f"Errors: {stats.Errors} ({stats.Errors / stats.Total * 100:.2f}%)")
        avg_latency = stats.TotalLatency / stats.Total
        print(f"Average latency: {avg_latency}")
    print(f"Total time: {elapsed:.2f}s")
    if elapsed > 0:
        throughput = stats.Total / elapsed
        print(f"Throughput: {throughput:.2f} checks/second")


if __name__ == "__main__":
    main()

