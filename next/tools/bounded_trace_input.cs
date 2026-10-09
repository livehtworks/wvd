using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

public static class WvdBoundedTraceInput {
    private sealed class PrefixStream : Stream {
        private readonly Stream source; private long remaining;
        public PrefixStream(Stream source, long bytes) { this.source = source; remaining = bytes; }
        public override int Read(byte[] buffer, int offset, int count) {
            if (remaining == 0) return 0;
            int read = source.Read(buffer, offset, (int)Math.Min(remaining, count));
            if (read == 0) throw new IOException("TRACE_EVENT_PREFIX_SHORT_READ");
            remaining -= read; return read;
        }
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }
    public static IEnumerable<string> Lines(string path, long maxBytes, int maxRecords, int maxLineChars) {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("TRACE_INPUT_REPARSE_POINT");
        using (var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
            if (file.Length > maxBytes) throw new IOException("TRACE_EVENT_BYTES_EXCEEDED");
            using (var reader = new StreamReader(new PrefixStream(file, file.Length), new UTF8Encoding(false, true), false, 4096)) {
                var chunk = new char[4096]; var row = new StringBuilder(); int count, records = 0;
                while ((count = reader.Read(chunk, 0, chunk.Length)) != 0) {
                    for (int i = 0; i < count; ++i) {
                        if (chunk[i] == '\n') {
                            if (++records > maxRecords) throw new IOException("TRACE_EVENT_RECORDS_EXCEEDED");
                            yield return row.ToString().TrimEnd('\r'); row.Clear();
                        } else {
                            if (row.Length >= maxLineChars) throw new IOException("TRACE_EVENT_LINE_EXCEEDED");
                            row.Append(chunk[i]);
                        }
                    }
                }
                if (row.Length != 0) throw new IOException("TRACE_EVENT_PARTIAL_LINE");
            }
        }
    }
}
