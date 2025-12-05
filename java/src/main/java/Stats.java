import java.time.Duration;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.ReentrantLock;

public class Stats {
    public AtomicLong Total = new AtomicLong(0);
    public AtomicLong Success = new AtomicLong(0);
    public AtomicLong Failures = new AtomicLong(0);
    public AtomicLong Errors = new AtomicLong(0);
    public Duration TotalLatency = Duration.ZERO;
    public ReentrantLock mu = new ReentrantLock();

    public Stats copy() {
        mu.lock();
        try {
            Stats copy = new Stats();
            copy.Total = new AtomicLong(Total.get());
            copy.Success = new AtomicLong(Success.get());
            copy.Failures = new AtomicLong(Failures.get());
            copy.Errors = new AtomicLong(Errors.get());
            copy.TotalLatency = TotalLatency;
            return copy;
        } finally {
            mu.unlock();
        }
    }
}

