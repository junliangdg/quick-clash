param([Parameter(Mandatory)][int]$ProcessId, [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SubscriptionMemoryMaps {
    [StructLayout(LayoutKind.Sequential)] struct Region {
        public IntPtr Address, Allocation; public uint AllocationProtect;
        public UIntPtr Size; public uint State, Protect, Type;
    }
    [StructLayout(LayoutKind.Sequential)] struct Page { public IntPtr Address; public UIntPtr Flags; }
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern UIntPtr VirtualQueryEx(IntPtr process, IntPtr address, out Region region, UIntPtr size);
    [DllImport("psapi.dll", CharSet=CharSet.Unicode)] static extern uint GetMappedFileNameW(IntPtr process, IntPtr address, StringBuilder name, uint size);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSetEx(IntPtr process, [In,Out] Page[] pages, int bytes);
    public class Entry {
        public string Name; public ulong Committed, Resident, PrivateResident, Regions;
    }
    public static Entry[] Read(int pid) {
        var process = OpenProcess(0x410, false, pid);
        if (process == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        var entries = new Dictionary<string, Entry>();
        try {
            ulong address=0; Region region;
            while (VirtualQueryEx(process, (IntPtr)(long)address, out region, (UIntPtr)Marshal.SizeOf<Region>()).ToUInt64()!=0) {
                ulong size=region.Size.ToUInt64(), next=(ulong)region.Address.ToInt64()+size;
                if (next<=address) break;
                address=next;
                if (region.State!=0x1000) continue;
                string name=region.Type==0x20000 ? "[private]" : region.Type==0x40000 ? "[mapped]" : "[image]";
                if (region.Type!=0x20000) {
                    var path=new StringBuilder(2048);
                    if (GetMappedFileNameW(process, region.Address, path, 2048)!=0) name=System.IO.Path.GetFileName(path.ToString());
                }
                Entry entry;
                if (!entries.TryGetValue(name,out entry)) entries[name]=entry=new Entry{Name=name};
                entry.Committed+=size; entry.Regions++;
                for (ulong offset=0; offset<size;) {
                    int count=(int)Math.Min(4096UL,(size-offset+4095)/4096);
                    var pages=new Page[count];
                    for(int i=0;i<count;i++) pages[i].Address=(IntPtr)(region.Address.ToInt64()+(long)offset+i*4096L);
                    if (!QueryWorkingSetEx(process,pages,count*Marshal.SizeOf<Page>())) throw new System.ComponentModel.Win32Exception();
                    foreach(var page in pages) {
                        ulong flags=page.Flags.ToUInt64();
                        if((flags&1)==0) continue;
                        entry.Resident+=4096;
                        if((flags&(1UL<<15))==0) entry.PrivateResident+=4096;
                    }
                    offset+=(ulong)count*4096;
                }
            }
        } finally { CloseHandle(process); }
        var result=new Entry[entries.Count]; entries.Values.CopyTo(result,0); return result;
    }
}
'@
$process = Get-Process -Id $ProcessId
$entries = @([SubscriptionMemoryMaps]::Read($ProcessId) | Sort-Object Resident -Descending)
$result = [ordered]@{
    pid = $ProcessId
    executable = [IO.Path]::GetFileName($process.Path)
    sampled_at = [DateTimeOffset]::Now.ToString('o')
    working_set = $process.WorkingSet64
    private_commit = $process.PrivateMemorySize64
    entries = $entries
    limitations = 'Read-only VirtualQueryEx/QueryWorkingSetEx; file basenames only, no content, dumps, commands or URLs. Non-atomic page samples. Shared pages may be shareable rather than actively shared.'
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$entries | Select-Object -First 18 | Format-Table
