import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

// Runner performs ?
public class Runner {
    private HttpClient client;
    private Duration timeout;
    private int max;
    private BlockingQueue<Rslt> rslts;
    private Stats stats;

    public Runner(Duration timeout, int max) {
        try {
            // Create SSL context that accepts all certificates (equivalent to InsecureSkipVerify)
            SSLContext sslContext = SSLContext.getInstance("TLS");
            TrustManager[] trustAllCerts = new TrustManager[]{
                new X509TrustManager() {
                    public java.security.cert.X509Certificate[] getAcceptedIssuers() {
                        return null;
                    }
                    public void checkClientTrusted(java.security.cert.X509Certificate[] certs, String authType) {
                    }
                    public void checkServerTrusted(java.security.cert.X509Certificate[] certs, String authType) {
                    }
                }
            };
            sslContext.init(null, trustAllCerts, new java.security.SecureRandom());

            this.client = HttpClient.newBuilder()
                    .connectTimeout(timeout)
                    .sslContext(sslContext)
                    .build();
        } catch (Exception e) {
            throw new RuntimeException("Failed to create HTTP client", e);
        }

        this.timeout = timeout;
        this.max = max;
        this.rslts = new LinkedBlockingQueue<>(1000);
        this.stats = new Stats();
    }

    // Chk performs ?
    public Rslt Chk(Rsr rsr) {
        Instant start = Instant.now();
        Rslt rslt = new Rslt();
        rslt.Rsr = rsr;
        rslt.Timestamp = Instant.now();

        try {
            HttpRequest req = HttpRequest.newBuilder()
                    .uri(URI.create(rsr.Dest))
                    .timeout(timeout)
                    .header("User-Agent", "Checker/1.0")
                    .header("Connection", "close")
                    .GET()
                    .build();

            HttpResponse<Void> resp = client.send(req, HttpResponse.BodyHandlers.discarding());

            rslt.ChkLatency = Duration.between(start, Instant.now());
            rslt.IsChkSuccess = resp.statusCode() >= 200 && resp.statusCode() < 400;
        } catch (Exception e) {
            rslt.Error = e;
            rslt.IsChkSuccess = false;
            rslt.ChkLatency = Duration.between(start, Instant.now());
        }

        return rslt;
    }

    // CheckRsr performs ?
    public void CheckRsr(List<Rsr> rsrs) {
        Semaphore sem = new Semaphore(max);
        ExecutorService executor = Executors.newFixedThreadPool(max);
        CountDownLatch latch = new CountDownLatch(rsrs.size());

        for (Rsr rsr : rsrs) {
            sem.acquireUninterruptibly();
            executor.submit(() -> {
                try {
                    Rslt result = Chk(rsr);
                    rslts.offer(result);

                    stats.mu.lock();
                    try {
                        stats.Total.incrementAndGet();
                        if (result.IsChkSuccess) {
                            stats.Success.incrementAndGet();
                        } else {
                            stats.Failures.incrementAndGet();
                        }
                        if (result.Error != null) {
                            stats.Errors.incrementAndGet();
                        }
                        stats.TotalLatency = stats.TotalLatency.plus(result.ChkLatency);
                    } finally {
                        stats.mu.unlock();
                    }
                } finally {
                    sem.release();
                    latch.countDown();
                }
            });
        }

        try {
            latch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            System.err.printf("Error during checks: %s%n", e.getMessage());
        } finally {
            executor.shutdown();
            try {
                executor.awaitTermination(1, TimeUnit.MINUTES);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    public Stats GetStats() {
        return stats.copy();
    }
}

