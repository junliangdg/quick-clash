#Requires -Version 7.2
if (-not ('QuickClash.Diagnostics.ProcessMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace QuickClash.Diagnostics {
    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessMemoryCounters {
        public uint cb;
        public uint PageFaultCount;
        public UIntPtr PeakWorkingSetSize;
        public UIntPtr WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage;
        public UIntPtr QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage;
        public UIntPtr QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage;
        public UIntPtr PeakPagefileUsage;
        public UIntPtr PrivateUsage;
        public UIntPtr PrivateWorkingSetSize;
        public ulong SharedCommitUsage;
    }
    public static class ProcessMemory {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern Microsoft.Win32.SafeHandles.SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", EntryPoint = "K32GetProcessMemoryInfo", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadCounters(IntPtr process, ref ProcessMemoryCounters counters, uint size);
        public static ProcessMemoryCounters Read(IntPtr process) {
            var counters = new ProcessMemoryCounters();
            counters.cb = (uint)Marshal.SizeOf<ProcessMemoryCounters>();
            if (!ReadCounters(process, ref counters, counters.cb))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return counters;
        }
        public static ProcessMemoryCounters ReadPid(int pid) {
            // Query-only access: no process control, injection or working-set trimming rights.
            using (var process = OpenProcess(0x1000, false, pid)) {
                if (process.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                return Read(process.DangerousGetHandle());
            }
        }
    }
}
'@
}
