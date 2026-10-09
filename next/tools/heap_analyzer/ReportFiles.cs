using System.Text;
using System.Text.Json;

namespace Wvd.HeapAnalyzer;

internal sealed class ReportFiles
{
    private static long inputBytes;
    private static int inputFiles;
    private readonly string root;
    private readonly long limit;
    public ReportFiles(string root, long limit) { this.root = root; this.limit = limit; }
    public void Write(string name, Action<StreamWriter> write)
    {
        var partial = Path.Combine(root, name + ".partial");
        using (var file = new FileStream(partial, FileMode.CreateNew, FileAccess.Write))
        using (var bounded = new BoundedStream(file, limit - DirectoryBytes()))
        using (var writer = new StreamWriter(bounded, new UTF8Encoding(false))) { write(writer); }
        File.Move(partial, Path.Combine(root, name));
    }
    public void Json(string name, object value)
    {
        var partial = Path.Combine(root, name + ".partial");
        using (var file = new FileStream(partial, FileMode.CreateNew, FileAccess.Write))
        using (var bounded = new BoundedStream(file, limit - DirectoryBytes()))
            JsonSerializer.Serialize(bounded, value, new JsonSerializerOptions { WriteIndented = true });
        File.Move(partial, Path.Combine(root, name));
    }
    public static JsonDocument ReadJson(string path)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("ANALYZER_JSON_INPUT_REPARSE_POINT");
        if (file.Length > 4L * 1024 * 1024) throw new InvalidDataException("ANALYZER_JSON_INPUT_BUDGET_EXCEEDED");
        if (Interlocked.Increment(ref inputFiles) > 256 ||
            Interlocked.Add(ref inputBytes, file.Length) > 32L * 1024 * 1024)
            throw new InvalidDataException("ANALYZER_JSON_TOTAL_BUDGET_EXCEEDED");
        return JsonDocument.Parse(file, new JsonDocumentOptions { MaxDepth = 64 });
    }
    public long DirectoryBytes() => Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
        .Sum(p => new FileInfo(p).Length);
    public static string Csv(string value) => "\"" + value.Replace("\"", "\"\"") + "\"";

    private sealed class BoundedStream(Stream inner, long remaining) : Stream
    {
        private long written;
        public override void Write(byte[] buffer, int offset, int count)
        {
            if (count > remaining - written) throw new IOException("ANALYZER_OUTPUT_BUDGET_EXCEEDED");
            inner.Write(buffer, offset, count); written += count;
        }
        public override void Write(ReadOnlySpan<byte> buffer)
        {
            if (buffer.Length > remaining - written) throw new IOException("ANALYZER_OUTPUT_BUDGET_EXCEEDED");
            inner.Write(buffer); written += buffer.Length;
        }
        public override void Flush() => inner.Flush();
        public override bool CanRead => false;
        public override bool CanSeek => false;
        public override bool CanWrite => true;
        public override long Length => written;
        public override long Position { get => written; set => throw new NotSupportedException(); }
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
    }
}
