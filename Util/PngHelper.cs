using System;
using System.IO;

namespace SuperSkillTool
{
    /// <summary>
    /// Loads a PNG file and returns its bytes or base64 string.
    /// </summary>
    public static class PngHelper
    {
        /// <summary>
        /// Reads the file at <paramref name="path"/> and returns the raw bytes.
        /// Returns null if the file does not exist.
        /// </summary>
        public static byte[] LoadPngBytes(string path)
        {
            if (string.IsNullOrEmpty(path)) return null;

            // Resolve relative to tool root if not absolute
            if (!Path.IsPathRooted(path))
                path = Path.Combine(PathConfig.ToolRoot, path);

            if (!File.Exists(path))
            {
                Console.WriteLine($"  [warn] Icon file not found: {path}");
                return null;
            }

            return File.ReadAllBytes(path);
        }

        /// <summary>
        /// Loads a PNG and returns it as a base64-encoded string.
        /// Returns empty string on failure.
        /// </summary>
        public static string LoadPngBase64(string path)
        {
            byte[] data = LoadPngBytes(path);
            if (data == null || data.Length == 0) return "";
            return Convert.ToBase64String(data);
        }

        /// <summary>
        /// Generates a tiny 32x32 single-color PNG for placeholder icons.
        /// The PNG has a minimal valid structure (IHDR + IDAT + IEND).
        /// </summary>
        public static byte[] GeneratePlaceholderPng(byte r, byte g, byte b, byte a = 255)
        {
            // Build raw scanlines: 32 rows, each with filter byte 0 + 32 RGBA pixels
            int rowBytes = 1 + 32 * 4; // filter byte + 32 pixels * 4 channels
            byte[] raw = new byte[32 * rowBytes];
            for (int y = 0; y < 32; y++)
            {
                int rowStart = y * rowBytes;
                raw[rowStart] = 0; // filter none
                for (int x = 0; x < 32; x++)
                {
                    int px = rowStart + 1 + x * 4;
                    raw[px + 0] = r;
                    raw[px + 1] = g;
                    raw[px + 2] = b;
                    raw[px + 3] = a;
                }
            }

            // Deflate using DeflateStream
            byte[] compressed;
            using (var ms = new MemoryStream())
            {
                // zlib header: CMF=0x78, FLG=0x01
                ms.WriteByte(0x78);
                ms.WriteByte(0x01);
                using (var ds = new System.IO.Compression.DeflateStream(ms,
                    System.IO.Compression.CompressionMode.Compress, leaveOpen: true))
                {
                    ds.Write(raw, 0, raw.Length);
                }
                // Adler-32 checksum
                uint adler = Adler32(raw);
                ms.WriteByte((byte)(adler >> 24));
                ms.WriteByte((byte)(adler >> 16));
                ms.WriteByte((byte)(adler >> 8));
                ms.WriteByte((byte)(adler));
                compressed = ms.ToArray();
            }

            // Build PNG file
            using (var png = new MemoryStream())
            {
                // Signature
                png.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8);

                // IHDR
                WriteChunk(png, "IHDR", writer =>
                {
                    WriteBE32(writer, 32);  // width
                    WriteBE32(writer, 32);  // height
                    writer.WriteByte(8);    // bit depth
                    writer.WriteByte(6);    // color type: RGBA
                    writer.WriteByte(0);    // compression
                    writer.WriteByte(0);    // filter
                    writer.WriteByte(0);    // interlace
                });

                // IDAT
                WriteChunk(png, "IDAT", writer =>
                {
                    writer.Write(compressed, 0, compressed.Length);
                });

                // IEND
                WriteChunk(png, "IEND", writer => { });

                return png.ToArray();
            }
        }

        public static string GeneratePlaceholderPngBase64(byte r, byte g, byte b, byte a = 255)
        {
            return Convert.ToBase64String(GeneratePlaceholderPng(r, g, b, a));
        }

        // ── PNG chunk helpers ──────────────────────────────────

        private static void WriteChunk(MemoryStream png, string type, Action<MemoryStream> writeData)
        {
            using (var data = new MemoryStream())
            {
                writeData(data);
                byte[] dataBytes = data.ToArray();
                byte[] typeBytes = System.Text.Encoding.ASCII.GetBytes(type);

                // Length (4 bytes BE)
                WriteBE32(png, (uint)dataBytes.Length);
                // Type
                png.Write(typeBytes, 0, 4);
                // Data
                if (dataBytes.Length > 0)
                    png.Write(dataBytes, 0, dataBytes.Length);
                // CRC over type + data
                uint crc = Crc32(typeBytes, dataBytes);
                WriteBE32(png, crc);
            }
        }

        private static void WriteBE32(Stream s, uint value)
        {
            s.WriteByte((byte)(value >> 24));
            s.WriteByte((byte)(value >> 16));
            s.WriteByte((byte)(value >> 8));
            s.WriteByte((byte)(value));
        }

        // ── CRC-32 (PNG uses CRC-32/ISO-HDLC) ────────────────

        private static readonly uint[] Crc32Table = BuildCrc32Table();

        private static uint[] BuildCrc32Table()
        {
            var t = new uint[256];
            for (uint n = 0; n < 256; n++)
            {
                uint c = n;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[n] = c;
            }
            return t;
        }

        private static uint Crc32(byte[] typeBytes, byte[] dataBytes)
        {
            uint crc = 0xFFFFFFFF;
            foreach (byte b in typeBytes)
                crc = Crc32Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
            foreach (byte b in dataBytes)
                crc = Crc32Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFF;
        }

        // ── Adler-32 ──────────────────────────────────────────

        private static uint Adler32(byte[] data)
        {
            uint a = 1, b = 0;
            foreach (byte d in data)
            {
                a = (a + d) % 65521;
                b = (b + a) % 65521;
            }
            return (b << 16) | a;
        }
    }
}
