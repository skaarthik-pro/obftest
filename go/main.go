package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"io"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"golang.org/x/sync/errgroup"
)

// Rsr represents ?
type Rsr struct {
	ID   string
	Dest string
}

// Rslt represents ?
type Rslt struct {
	Rsr          Rsr
	IsChkSuccess bool
	ChkLatency   time.Duration
	Error        error
	Timestamp    time.Time
}

// Runner performs ?
type Runner struct {
	client  *http.Client
	timeout time.Duration
	max     int
	rslts   chan Rslt
	//wg            sync.WaitGroup
	stats *Stats
}

type Stats struct {
	Total        int64
	Success      int64
	Failures     int64
	Errors       int64
	TotalLatency time.Duration
	mu           sync.Mutex
}

func NewRunner(timeout time.Duration, max int) *Runner {
	transport := &http.Transport{
		MaxIdleConns:        1000,
		MaxIdleConnsPerHost: 100,
		IdleConnTimeout:     90 * time.Second,
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true,
		},
	}

	client := &http.Client{
		Transport: transport,
		Timeout:   timeout,
	}

	return &Runner{
		client:  client,
		timeout: timeout,
		max:     max,
		rslts:   make(chan Rslt, 1000),
		stats:   &Stats{},
	}
}

// Chk performs ?
func (r *Runner) Chk(ctx context.Context, rsr Rsr) Rslt {
	start := time.Now()
	rslt := Rslt{
		Rsr:       rsr,
		Timestamp: time.Now(),
	}

	req, err := http.NewRequestWithContext(ctx, "GET", rsr.Dest, nil)
	if err != nil {
		rslt.Error = err
		return rslt
	}

	// Set headers for health check
	req.Header.Set("User-Agent", "Checker/1.0")
	req.Header.Set("Connection", "close")

	resp, err := r.client.Do(req)
	if err != nil {
		rslt.Error = err
		rslt.IsChkSuccess = false
		rslt.ChkLatency = time.Since(start)
		return rslt
	}
	defer resp.Body.Close()

	//?
	_, _ = io.CopyN(io.Discard, resp.Body, 1024)

	rslt.ChkLatency = time.Since(start)
	rslt.IsChkSuccess = resp.StatusCode >= 200 && resp.StatusCode < 400

	return rslt
}

// CheckRsr performs ?
func (r *Runner) CheckRsr(ctx context.Context, rsrs []Rsr) {

	sem := make(chan struct{}, r.max)

	g, ctx := errgroup.WithContext(ctx)

	for _, rsr := range rsrs {
		rsr := rsr
		sem <- struct{}{}

		g.Go(func() error {
			defer func() { <-sem }()

			result := r.Chk(ctx, rsr)
			r.rslts <- result

			r.stats.mu.Lock()
			atomic.AddInt64(&r.stats.Total, 1)
			if result.IsChkSuccess {
				atomic.AddInt64(&r.stats.Success, 1)
			} else {
				atomic.AddInt64(&r.stats.Failures, 1)
			}
			if result.Error != nil {
				atomic.AddInt64(&r.stats.Errors, 1)
			}
			r.stats.TotalLatency += result.ChkLatency
			r.stats.mu.Unlock()

			return nil
		})
	}

	if err := g.Wait(); err != nil {
		fmt.Printf("Error during checks: %v\n", err)
	}

	close(r.rslts)
}

func (r *Runner) GetStats() Stats {
	r.stats.mu.Lock()
	defer r.stats.mu.Unlock()
	return Stats{
		Total:        r.stats.Total,
		Success:      r.stats.Success,
		Failures:     r.stats.Failures,
		Errors:       r.stats.Errors,
		TotalLatency: r.stats.TotalLatency,
	}
}

func GenerateRsrs(baseURL string, count int) []Rsr {
	rsrs := make([]Rsr, count)
	for i := 0; i < count; i++ {
		rsrs[i] = Rsr{
			ID:   fmt.Sprintf("rsr-%d", i+1),
			Dest: fmt.Sprintf("%s/health", baseURL),
		}
	}
	return rsrs
}

func main() {
	var (
		rsrCount       = flag.Int("count", 100000, "Number of Rsrs")
		baseURL        = flag.String("base-url", "http://localhost:8080", "Base URL")
		max            = flag.Int("max", 1000, "Maximum")
		timeout        = flag.Duration("timeout", 5*time.Second, "Timeout for each check")
		reportInterval = flag.Duration("report-interval", 5*time.Second, "Interval for progress reports")
	)
	flag.Parse()

	rsrs := GenerateRsrs(*baseURL, *rsrCount)

	checker := NewRunner(*timeout, *max)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	done := make(chan struct{})
	go func() {
		ticker := time.NewTicker(*reportInterval)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				stats := checker.GetStats()
				if stats.Total > 0 {
					avgLatency := stats.TotalLatency / time.Duration(stats.Total)
					fmt.Printf("[Progress] Total: %d | Success: %d | Failures: %d | Errors: %d | Avg Latency: %v\n",
						stats.Total, stats.Success, stats.Failures, stats.Errors, avgLatency)
				}
			case <-done:
				return
			}
		}
	}()

	startTime := time.Now()
	checker.CheckRsr(ctx, rsrs)
	close(done)

	elapsed := time.Since(startTime)
	stats := checker.GetStats()

	fmt.Println("\n=== Final Results ===")
	fmt.Printf("Total Rsr checked: %d\n", stats.Total)
	fmt.Printf("Success: %d (%.2f%%)\n", stats.Success, float64(stats.Success)/float64(stats.Total)*100)
	fmt.Printf("Failures: %d (%.2f%%)\n", stats.Failures, float64(stats.Failures)/float64(stats.Total)*100)
	fmt.Printf("Errors: %d (%.2f%%)\n", stats.Errors, float64(stats.Errors)/float64(stats.Total)*100)
	if stats.Total > 0 {
		avgLatency := stats.TotalLatency / time.Duration(stats.Total)
		fmt.Printf("Average latency: %v\n", avgLatency)
	}
	fmt.Printf("Total time: %v\n", elapsed)
	fmt.Printf("Throughput: %.2f checks/second\n", float64(stats.Total)/elapsed.Seconds())
}
