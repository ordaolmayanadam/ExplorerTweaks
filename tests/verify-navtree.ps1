# Explorer sol menüsünü (SysTreeView32) dışarıdan, salt okunur okuyup beklenen düzenle karşılaştırır.
# Kullanım: powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-navtree.ps1 `
#             -Expected 'This PC','Phone','Linux' -Pinned bottom -NoPinIcons
# Çıktı: PASS (çıkış kodu 0) ya da FAIL: <neden> (çıkış kodu 1).
param(
    [string[]]$Expected = @('This PC', 'Phone', 'Linux'),
    [ValidateSet('bottom', 'top', 'default')][string]$Pinned = 'default',
    [switch]$NoPinIcons
)

# `powershell -File` ile gelen 'a','b' listesi tek bir "a,b" dizgesi olur; virgülden böl.
$Expected = @($Expected | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}

Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class NavTree {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, int s, uint t, uint pr);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, int s, uint t);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr w);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  const int TVM_GETNEXTITEM = 0x110A, TVM_GETITEMW = 0x113E, TVM_GETIMAGELIST = 0x1108;
  const int TVGN_ROOT = 0, TVGN_NEXT = 1, TVGN_CHILD = 4, TVSIL_STATE = 2;

  public static IntPtr FindTree(IntPtr top) {
    IntPtr r = IntPtr.Zero;
    EnumChildWindows(top, (h, l) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "SysTreeView32") { r = h; return false; } return true; }, IntPtr.Zero);
    return r;
  }

  // TVM_GETIMAGELIST(TVSIL_STATE); 0 ise durum image list'i yok (pin ikonları kaldırılmış).
  public static long StateImageList(IntPtr tree) {
    return SendMessage(tree, TVM_GETIMAGELIST, (IntPtr)TVSIL_STATE, IntPtr.Zero).ToInt64();
  }

  // Gizli kökün (Desktop) çocukları: "ad|pinli" satırları, ağaçtaki sırayla.
  public static List<string> RootChildren(IntPtr tree) {
    var result = new List<string>();
    uint pid; GetWindowThreadProcessId(tree, out pid);
    IntPtr proc = OpenProcess(0x0008 | 0x0010 | 0x0020, false, pid); // VM_OPERATION|VM_READ|VM_WRITE
    if (proc == IntPtr.Zero) throw new Exception("OpenProcess failed");
    IntPtr mem = VirtualAllocEx(proc, IntPtr.Zero, 4096, 0x3000, 0x04);
    try {
      IntPtr root = SendMessage(tree, TVM_GETNEXTITEM, (IntPtr)TVGN_ROOT, IntPtr.Zero);
      if (root == IntPtr.Zero) return result;
      IntPtr item = SendMessage(tree, TVM_GETNEXTITEM, (IntPtr)TVGN_CHILD, root);
      while (item != IntPtr.Zero) {
        // TVITEMW (x64): mask(0) pad(4) hItem(8) state(16) stateMask(20) pszText(24) cchTextMax(32) iImage(36) iSelectedImage(40) cChildren(44) lParam(48)
        byte[] tv = new byte[56];
        BitConverter.GetBytes(0x0001 | 0x0008).CopyTo(tv, 0); // TVIF_TEXT|TVIF_STATE
        BitConverter.GetBytes(item.ToInt64()).CopyTo(tv, 8);
        BitConverter.GetBytes(0xFFFF).CopyTo(tv, 20);          // stateMask: all
        BitConverter.GetBytes(mem.ToInt64() + 256).CopyTo(tv, 24);
        BitConverter.GetBytes(512).CopyTo(tv, 32);
        IntPtr n; WriteProcessMemory(proc, mem, tv, tv.Length, out n);
        SendMessage(tree, TVM_GETITEMW, IntPtr.Zero, mem);
        ReadProcessMemory(proc, mem, tv, tv.Length, out n);
        byte[] txt = new byte[1024]; ReadProcessMemory(proc, (IntPtr)(mem.ToInt64() + 256), txt, 1024, out n);
        string s = Encoding.Unicode.GetString(txt); int z = s.IndexOf('\0'); if (z >= 0) s = s.Substring(0, z);
        uint state = BitConverter.ToUInt32(tv, 16);
        bool pinned = ((state >> 12) & 0xF) == 1;
        result.Add(s + "|" + (pinned ? "1" : "0"));
        item = SendMessage(tree, TVM_GETNEXTITEM, (IntPtr)TVGN_NEXT, item);
      }
    } finally { VirtualFreeEx(proc, mem, 0, 0x8000); CloseHandle(proc); }
    return result;
  }
}
"@

function Fail([string]$reason) {
    Write-Output "FAIL: $reason"
    exit 1
}

try {
    $top = [IntPtr](Get-Process explorer -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle -like '*File Explorer*' -and $_.MainWindowHandle -ne 0 } |
        Select-Object -First 1).MainWindowHandle
    if ($top -eq [IntPtr]::Zero) { Fail 'no File Explorer window found' }

    $tree = [NavTree]::FindTree($top)
    if ($tree -eq [IntPtr]::Zero) { Fail 'navigation tree (SysTreeView32) not found' }

    $items = foreach ($line in [NavTree]::RootChildren($tree)) {
        $i = $line.LastIndexOf('|')
        [pscustomobject]@{ name = $line.Substring(0, $i); pinned = ($line.Substring($i + 1) -eq '1') }
    }
    $items = @($items)
    if ($items.Count -eq 0) { Fail 'navigation tree has no root children' }
    $actual = ($items | ForEach-Object { if ($_.pinned) { "[pin]$($_.name)" } else { $_.name } }) -join ', '

    # (a) Pinli olmayanların ilk N tanesi -Expected ile birebir aynı mı?
    $unpinned = @($items | Where-Object { -not $_.pinned } | ForEach-Object { $_.name })
    for ($k = 0; $k -lt $Expected.Count; $k++) {
        if ($k -ge $unpinned.Count -or $unpinned[$k] -cne $Expected[$k]) {
            Fail "unpinned item #$($k + 1) expected '$($Expected[$k])'; tree is: $actual"
        }
    }

    # (b) Pinlilerin yeri
    $pinIdx = @(0..($items.Count - 1) | Where-Object { $items[$_].pinned })
    $unpinIdx = @(0..($items.Count - 1) | Where-Object { -not $items[$_].pinned })
    if ($pinIdx.Count -gt 0 -and $unpinIdx.Count -gt 0) {
        if ($Pinned -eq 'bottom' -and ($pinIdx | Measure-Object -Minimum).Minimum -lt ($unpinIdx | Measure-Object -Maximum).Maximum) {
            Fail "pinned items are not all after unpinned ones; tree is: $actual"
        }
        if ($Pinned -eq 'top' -and ($pinIdx | Measure-Object -Maximum).Maximum -gt ($unpinIdx | Measure-Object -Minimum).Minimum) {
            Fail "pinned items are not all before unpinned ones; tree is: $actual"
        }
    }

    # (c) Pin ikonları (state image list) kaldırılmış mı?
    if ($NoPinIcons) {
        $il = [NavTree]::StateImageList($tree)
        if ($il -ne 0) { Fail "state image list is still set (0x$($il.ToString('X')))" }
    }

    Write-Output 'PASS'
    exit 0
}
catch {
    Fail "exception: $($_.Exception.Message)"
}
