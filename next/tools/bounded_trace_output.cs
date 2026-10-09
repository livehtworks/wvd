using System;
using System.IO;
using System.Diagnostics;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

// Two fixed-size buffers share one output allowance; no ReadToEnd task owns
// an unbounded parent-process string while the child is running.
public sealed class WvdTraceOutput : IDisposable {
    readonly object sync = new object();
    readonly FileStream file;
    readonly Stream stdout, stderr;
    readonly CancellationTokenSource stop = new CancellationTokenSource();
    readonly Task first, second;
    readonly long limit;
    long bytes;
    int failed;
    bool sealedOutput;
    public bool Failed { get { return Volatile.Read(ref failed) != 0; } }
    public long Bytes { get { return Interlocked.Read(ref bytes); } }
    public bool Complete { get { return first.IsCompleted && second.IsCompleted; } }
    public WvdTraceOutput(WvdTraceProcess process, string path, long maximum) {
        if (maximum < 1 || maximum > 128L*1024*1024) throw new ArgumentException("TRACE_OUTPUT_BUDGET_INVALID");
        limit = maximum;
        stdout = process.StandardOutput;
        stderr = process.StandardError;
        file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read, 16384);
        first = Pump(stdout); second = Pump(stderr);
    }
    async Task Pump(Stream source) {
        var buffer = new byte[16384];
        try {
            while (!stop.IsCancellationRequested) {
                int count = await source.ReadAsync(buffer, 0, buffer.Length, stop.Token).ConfigureAwait(false);
                if (count == 0) break;
                lock (sync) {
                    var accepted = (int)Math.Min(count, limit-bytes);
                    if (accepted > 0) { file.Write(buffer, 0, accepted); bytes += accepted; }
                    if (accepted != count) { Interlocked.Exchange(ref failed, 1); stop.Cancel(); break; }
                }
            }
        } catch (OperationCanceledException) { }
          catch { Interlocked.Exchange(ref failed, 1); stop.Cancel(); }
    }
    public bool Finish(int milliseconds) {
        if (!Task.WaitAll(new[] { first, second }, milliseconds)) return false;
        lock (sync) {
            if (!sealedOutput) { file.Flush(); file.Dispose(); sealedOutput=true; }
        }
        return true;
    }
    public void Dispose() {
        stop.Cancel();
        stdout.Dispose(); stderr.Dispose();
        // The pumps do not hold sync while waiting for input. Closing the
        // output under sync prevents a late writer from using a released file.
        lock (sync) { file.Dispose(); }
    }
}
