import java.time.Duration;
import java.time.Instant;

// Rslt represents ?
public class Rslt {
    public Rsr Rsr;
    public boolean IsChkSuccess;
    public Duration ChkLatency;
    public Exception Error;
    public Instant Timestamp;

    public Rslt() {
    }

    public Rslt(Rsr Rsr, boolean IsChkSuccess, Duration ChkLatency, Exception Error, Instant Timestamp) {
        this.Rsr = Rsr;
        this.IsChkSuccess = IsChkSuccess;
        this.ChkLatency = ChkLatency;
        this.Error = Error;
        this.Timestamp = Timestamp;
    }
}

