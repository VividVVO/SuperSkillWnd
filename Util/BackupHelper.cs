using System;
using System.IO;

namespace SuperSkillTool
{
    /// <summary>
    /// Creates timestamped backups of files before modification.
    /// </summary>
    public static class BackupHelper
    {
        private static string _sessionTimestamp;

        /// <summary>
        /// Returns the backup directory for this session (one timestamp per run).
        /// </summary>
        public static string GetSessionBackupDir()
        {
            if (_sessionTimestamp == null)
                _sessionTimestamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");

            string dir = Path.Combine(PathConfig.BackupRoot, _sessionTimestamp);
            if (!Directory.Exists(dir))
                Directory.CreateDirectory(dir);
            return dir;
        }

        /// <summary>
        /// Backs up <paramref name="filePath"/> into the session backup folder.
        /// Preserves relative structure from drive root so names don't collide.
        /// Returns the backup path, or null if file did not exist.
        /// </summary>
        public static string Backup(string filePath)
        {
            if (!File.Exists(filePath)) return null;

            string backupDir = GetSessionBackupDir();

            // Build a safe relative path: replace : with _ and keep dir structure
            string safe = filePath.Replace(":", "_");
            string dest = Path.Combine(backupDir, safe);

            string destDir = Path.GetDirectoryName(dest);
            if (!Directory.Exists(destDir))
                Directory.CreateDirectory(destDir);

            File.Copy(filePath, dest, overwrite: true);
            Console.WriteLine($"  [backup] {filePath}");
            Console.WriteLine($"        -> {dest}");
            return dest;
        }
    }
}
