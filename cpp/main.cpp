#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <curl/curl.h>

using namespace std;
using namespace std::chrono;

// Rsr represents ?
struct Rsr {
    string ID;
    string Dest;
};

// Rslt represents ?
struct Rslt {
    Rsr Rsr;
    bool IsChkSuccess;
    duration<double> ChkLatency;
    string Error;
    time_point<system_clock> Timestamp;
};

class Stats {
public:
    atomic<int64_t> Total{0};
    atomic<int64_t> Success{0};
    atomic<int64_t> Failures{0};
    atomic<int64_t> Errors{0};
    duration<double> TotalLatency{duration<double>::zero()};
    mutex mu;

    Stats copy() {
        lock_guard<mutex> lock(mu);
        Stats copy;
        copy.Total = Total.load();
        copy.Success = Success.load();
        copy.Failures = Failures.load();
        copy.Errors = Errors.load();
        copy.TotalLatency = TotalLatency;
        return copy;
    }
};

// Runner performs ?
class Runner {
private:
    duration<double> timeout;
    int max;
    queue<Rslt> rslts;
    Stats* stats;
    mutex rslts_mutex;
    static bool curl_initialized;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        // Discard the response body (equivalent to io.CopyN(io.Discard, resp.Body, 1024))
        size_t total_size = size * nmemb;
        size_t to_read = min(total_size, static_cast<size_t>(1024));
        return to_read;
    }

public:
    Runner(duration<double> timeout, int max) : timeout(timeout), max(max), stats(new Stats()) {
        // Initialize curl globally (thread-safe, can be called multiple times)
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
        }
    }

    ~Runner() {
        delete stats;
    }

    // Chk performs ?
    Rslt Chk(const Rsr& rsr) {
        auto start = system_clock::now();
        Rslt rslt;
        rslt.Rsr = rsr;
        rslt.Timestamp = system_clock::now();
        rslt.IsChkSuccess = false;
        rslt.Error = "";

        CURL* easy_handle = curl_easy_init();
        if (!easy_handle) {
            rslt.Error = "Failed to create curl handle";
            rslt.ChkLatency = duration<double>(system_clock::now() - start);
            return rslt;
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: Checker/1.0");
        headers = curl_slist_append(headers, "Connection: close");

        curl_easy_setopt(easy_handle, CURLOPT_URL, rsr.Dest.c_str());
        curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT, static_cast<long>(timeout.count()));
        curl_easy_setopt(easy_handle, CURLOPT_CONNECTTIMEOUT, static_cast<long>(timeout.count()));

        CURLcode res = curl_easy_perform(easy_handle);
        
        long response_code = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &response_code);
            rslt.IsChkSuccess = (response_code >= 200 && response_code < 400);
        } else {
            rslt.Error = curl_easy_strerror(res);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(easy_handle);

        rslt.ChkLatency = duration<double>(system_clock::now() - start);
        return rslt;
    }

    // CheckRsr performs ?
    void CheckRsr(const vector<Rsr>& rsrs) {
        atomic<int> sem_count{0};
        condition_variable cv;
        mutex sem_mutex;
        vector<thread> threads;
        atomic<int> active_threads{0};

        for (const auto& rsr : rsrs) {
            // Wait for semaphore
            unique_lock<mutex> lock(sem_mutex);
            cv.wait(lock, [&] { return sem_count.load() < max; });
            sem_count++;
            active_threads++;
            lock.unlock();

            threads.emplace_back([this, rsr, &sem_count, &cv, &sem_mutex, &active_threads]() {
                Rslt result = this->Chk(rsr);
                
                {
                    lock_guard<mutex> lock(rslts_mutex);
                    if (rslts.size() < 1000) {
                        rslts.push(result);
                    }
                }

                stats->mu.lock();
                stats->Total++;
                if (result.IsChkSuccess) {
                    stats->Success++;
                } else {
                    stats->Failures++;
                }
                if (!result.Error.empty()) {
                    stats->Errors++;
                }
                stats->TotalLatency += result.ChkLatency;
                stats->mu.unlock();

                // Release semaphore
                {
                    lock_guard<mutex> lock(sem_mutex);
                    sem_count--;
                    active_threads--;
                }
                cv.notify_one();
            });
        }

        // Wait for all threads to complete
        for (auto& t : threads) {
            t.join();
        }
    }

    Stats GetStats() {
        return stats->copy();
    }
};

bool Runner::curl_initialized = false;

vector<Rsr> GenerateRsrs(const string& baseURL, int count) {
    vector<Rsr> rsrs;
    rsrs.reserve(count);
    for (int i = 0; i < count; i++) {
        ostringstream oss_id, oss_dest;
        oss_id << "rsr-" << (i + 1);
        oss_dest << baseURL << "/health";
        rsrs.push_back({oss_id.str(), oss_dest.str()});
    }
    return rsrs;
}

duration<double> parse_duration(const string& s) {
    if (s.empty()) return duration<double>(5.0); // default 5 seconds
    
    size_t pos = s.find_first_not_of("0123456789.");
    if (pos == string::npos) {
        return duration<double>(stod(s));
    }
    
    double value = stod(s.substr(0, pos));
    string unit = s.substr(pos);
    
    if (unit == "s" || unit == "S") {
        return duration<double>(value);
    } else if (unit == "ms" || unit == "MS") {
        return duration<double>(value / 1000.0);
    } else if (unit == "m" || unit == "M") {
        return duration<double>(value * 60.0);
    } else {
        return duration<double>(value);
    }
}

string format_duration(const duration<double>& d) {
    auto seconds = duration_cast<seconds>(d);
    auto ms = duration_cast<milliseconds>(d - seconds);
    ostringstream oss;
    oss << seconds.count() << "s";
    if (ms.count() > 0) {
        oss << ms.count() << "ms";
    }
    return oss.str();
}

int main(int argc, char* argv[]) {
    int rsrCount = 100000;
    string baseURL = "http://localhost:8080";
    int max = 1000;
    duration<double> timeout = duration<double>(5.0);
    duration<double> reportInterval = duration<double>(5.0);

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-count") == 0 && i + 1 < argc) {
            rsrCount = stoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-base-url") == 0 && i + 1 < argc) {
            baseURL = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-max") == 0 && i + 1 < argc) {
            max = stoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-timeout") == 0 && i + 1 < argc) {
            timeout = parse_duration(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-report-interval") == 0 && i + 1 < argc) {
            reportInterval = parse_duration(argv[i + 1]);
            i++;
        }
    }

    vector<Rsr> rsrs = GenerateRsrs(baseURL, rsrCount);
    Runner checker(timeout, max);

    atomic<bool> done{false};

    thread reporter_thread([&checker, &done, reportInterval]() {
        while (!done.load()) {
            this_thread::sleep_for(reportInterval);
            if (done.load()) {
                return;
            }
            Stats stats = checker.GetStats();
            if (stats.Total.load() > 0) {
                duration<double> avgLatency = stats.TotalLatency / stats.Total.load();
                cout << "[Progress] Total: " << stats.Total.load()
                     << " | Success: " << stats.Success.load()
                     << " | Failures: " << stats.Failures.load()
                     << " | Errors: " << stats.Errors.load()
                     << " | Avg Latency: " << format_duration(avgLatency) << endl;
            }
        }
    });

    auto startTime = system_clock::now();
    checker.CheckRsr(rsrs);
    done = true;

    reporter_thread.join();

    auto elapsed = system_clock::now() - startTime;
    Stats stats = checker.GetStats();

    cout << "\n=== Final Results ===" << endl;
    cout << "Total Rsr checked: " << stats.Total.load() << endl;
    if (stats.Total.load() > 0) {
        double success_pct = (static_cast<double>(stats.Success.load()) / stats.Total.load()) * 100.0;
        double failures_pct = (static_cast<double>(stats.Failures.load()) / stats.Total.load()) * 100.0;
        double errors_pct = (static_cast<double>(stats.Errors.load()) / stats.Total.load()) * 100.0;
        
        cout << fixed << setprecision(2);
        cout << "Success: " << stats.Success.load() << " (" << success_pct << "%)" << endl;
        cout << "Failures: " << stats.Failures.load() << " (" << failures_pct << "%)" << endl;
        cout << "Errors: " << stats.Errors.load() << " (" << errors_pct << "%)" << endl;
        
        duration<double> avgLatency = stats.TotalLatency / stats.Total.load();
        cout << "Average latency: " << format_duration(avgLatency) << endl;
    }
    
    auto elapsed_seconds = duration_cast<duration<double>>(elapsed).count();
    cout << "Total time: " << fixed << setprecision(2) << elapsed_seconds << "s" << endl;
    if (elapsed_seconds > 0) {
        double throughput = stats.Total.load() / elapsed_seconds;
        cout << "Throughput: " << fixed << setprecision(2) << throughput << " checks/second" << endl;
    }

    return 0;
}

