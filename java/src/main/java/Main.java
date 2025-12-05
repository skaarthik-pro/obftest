import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

public class Main {
    private static Duration parseDuration(String s) {
        if (s.endsWith("s")) {
            long seconds = Long.parseLong(s.substring(0, s.length() - 1));
            return Duration.ofSeconds(seconds);
        } else if (s.endsWith("ms")) {
            long millis = Long.parseLong(s.substring(0, s.length() - 2));
            return Duration.ofMillis(millis);
        } else if (s.endsWith("m")) {
            long minutes = Long.parseLong(s.substring(0, s.length() - 1));
            return Duration.ofMinutes(minutes);
        } else {
            // Try to parse as ISO-8601 duration
            return Duration.parse(s);
        }
    }

    public static List<Rsr> GenerateRsrs(String baseURL, int count) {
        List<Rsr> rsrs = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            rsrs.add(new Rsr(
                String.format("rsr-%d", i + 1),
                String.format("%s/health", baseURL)
            ));
        }
        return rsrs;
    }

    public static void main(String[] args) {
        int rsrCount = 100000;
        String baseURL = "http://localhost:8080";
        int max = 1000;
        Duration timeout = Duration.ofSeconds(5);
        Duration reportInterval = Duration.ofSeconds(5);

        // Parse command line arguments
        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "-count":
                    if (i + 1 < args.length) {
                        rsrCount = Integer.parseInt(args[i + 1]);
                        i++;
                    }
                    break;
                case "-base-url":
                    if (i + 1 < args.length) {
                        baseURL = args[i + 1];
                        i++;
                    }
                    break;
                case "-max":
                    if (i + 1 < args.length) {
                        max = Integer.parseInt(args[i + 1]);
                        i++;
                    }
                    break;
                case "-timeout":
                    if (i + 1 < args.length) {
                        timeout = parseDuration(args[i + 1]);
                        i++;
                    }
                    break;
                case "-report-interval":
                    if (i + 1 < args.length) {
                        reportInterval = parseDuration(args[i + 1]);
                        i++;
                    }
                    break;
            }
        }

        List<Rsr> rsrs = GenerateRsrs(baseURL, rsrCount);
        Runner checker = new Runner(timeout, max);

        AtomicBoolean done = new AtomicBoolean(false);

        Thread reporterThread = new Thread(() -> {
            while (!done.get()) {
                try {
                    Thread.sleep(reportInterval.toMillis());
                    if (done.get()) {
                        return;
                    }
                    Stats stats = checker.GetStats();
                    if (stats.Total.get() > 0) {
                        Duration avgLatency = stats.TotalLatency.dividedBy(stats.Total.get());
                        System.out.printf("[Progress] Total: %d | Success: %d | Failures: %d | Errors: %d | Avg Latency: %s%n",
                            stats.Total.get(), stats.Success.get(), stats.Failures.get(), stats.Errors.get(), avgLatency);
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        });
        reporterThread.setDaemon(true);
        reporterThread.start();

        Instant startTime = Instant.now();
        checker.CheckRsr(rsrs);
        done.set(true);

        Duration elapsed = Duration.between(startTime, Instant.now());
        Stats stats = checker.GetStats();

        System.out.println("\n=== Final Results ===");
        System.out.printf("Total Rsr checked: %d%n", stats.Total.get());
        if (stats.Total.get() > 0) {
            System.out.printf("Success: %d (%.2f%%)%n", stats.Success.get(), 
                (double) stats.Success.get() / stats.Total.get() * 100);
            System.out.printf("Failures: %d (%.2f%%)%n", stats.Failures.get(), 
                (double) stats.Failures.get() / stats.Total.get() * 100);
            System.out.printf("Errors: %d (%.2f%%)%n", stats.Errors.get(), 
                (double) stats.Errors.get() / stats.Total.get() * 100);
            Duration avgLatency = stats.TotalLatency.dividedBy(stats.Total.get());
            System.out.printf("Average latency: %s%n", avgLatency);
        }
        System.out.printf("Total time: %s%n", elapsed);
        if (elapsed.getSeconds() > 0 || elapsed.toMillis() > 0) {
            double throughput = (double) stats.Total.get() / (elapsed.toMillis() / 1000.0);
            System.out.printf("Throughput: %.2f checks/second%n", throughput);
        }
    }
}

