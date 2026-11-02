# Explorer sol menüsünü (SysTreeView32) dışarıdan, salt okunur okuyup beklenen düzenle karşılaştırır.
# Kullanım: powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-navtree.ps1 `
#             -Expected 'This PC','Phone','Linux' -Pinned bottom -NoPinIcons -Exact
#   -Expected   : pinli olmayan ilk öğelerin beklenen sırası
#   -Exact      : pinli olmayan öğe sayısı -Expected'daki sayıya eşit olmalı (fazlası FAIL)
#   -Pinned     : pinlilerin yeri (bottom|top|default)
#   -NoPinIcons : durum image list'i (pin ikonları) kaldırılmış olmalı
#   -Hwnd       : denetlenecek CabinetWClass penceresi (yoksa en üstteki görünür Explorer penceresi)
# İlk görünür öğe kontrolü her zaman yapılır (RT4 / boş nav pane gerilemesi): ağaç kaydırılmamışsa
# (dikey konum 0 ya da kaydırma aralığı yok) TVGN_FIRSTVISIBLE kök ya da kökün ilk çocuğu olmalı.
# Çıktı: ağaç dökümü ve PASS (çıkış kodu 0) ya da FAIL: <neden> (çıkış kodu 1).
param(
    [string[]]$Expected = @('This PC', 'Phone', 'Linux'),
    [ValidateSet('bottom', 'top', 'default')][string]$Pinned = 'default',
    [switch]$NoPinIcons,
    [switch]$Exact,
    [long]$Hwnd = 0
)

# `powershell -File` ile gelen 'a','b' listesi tek bir "a,b" dizgesi olur; virgülden böl.
$Expected = @($Expected | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}

Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class NavTree {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr h, int m, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
  [DllImport("user32.dll")] static extern bool GetScrollInfo(IntPtr h, int bar, ref SCROLLINFO si);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, int s, uint t, uint pr);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, int s, uint t);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr w);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int s, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  const int TVM_GETNEXTITEM = 0x110A, TVM_GETITEMW = 0x113E, TVM_GETIMAGELIST = 0x1108;
  const int TVGN_ROOT = 0, TVGN_NEXT = 1, TVGN_CHILD = 4, TVGN_FIRSTVISIBLE = 5, TVSIL_STATE = 2;
  const uint SMTO_ABORTIFHUNG = 2, TIMEOUT_MS = 2000;
  const int SB_VERT = 1; const uint SIF_ALL = 0x17;

  // Asılı bir Explorer bu betiği kilitlemesin diye her mesaj zaman aşımıyla gönderilir.
  static IntPtr Send(IntPtr h, int m, IntPtr w, IntPtr l) {
    IntPtr result;
    if (SendMessageTimeout(h, m, w, l, SMTO_ABORTIFHUNG, TIMEOUT_MS, out result) == IntPtr.Zero)
      throw new Exception("SendMessageTimeout(0x" + m.ToString("X") + ") failed or timed out (" + Marshal.GetLastWin32Error() + ")");
    return result;
  }

  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);

  // Görünür, simge durumunda olmayan File Explorer pencereleri (CabinetWClass), Z sırasıyla.
  // Explorer pencereleri çoğunlukla kabuk sürecinde olduğundan süreç ana penceresine bakılmaz.
  public static List<IntPtr> FindCabinets() {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "CabinetWClass" && IsWindowVisible(h) && !IsIconic(h)) list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }

  public static IntPtr FindTree(IntPtr top) {
    IntPtr r = IntPtr.Zero;
    EnumChildWindows(top, (h, l) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "SysTreeView32") { r = h; return false; } return true; }, IntPtr.Zero);
    return r;
  }

  // TVM_GETIMAGELIST(TVSIL_STATE); 0 ise durum image list'i yok (pin ikonları kaldırılmış).
  public static long StateImageList(IntPtr tree) {
    return Send(tree, TVM_GETIMAGELIST, (IntPtr)TVSIL_STATE, IntPtr.Zero).ToInt64();
  }

  public static IntPtr Next(IntPtr tree, int code, IntPtr item) {
    return Send(tree, TVM_GETNEXTITEM, (IntPtr)code, item);
  }

  // Dikey kaydırma konumu; kaydırma çubuğu ya da aralığı yoksa -1.
  public static int ScrollPos(IntPtr tree) {
    var si = new SCROLLINFO(); si.cbSize = (uint)Marshal.SizeOf(si); si.fMask = SIF_ALL;
    if (!GetScrollInfo(tree, SB_VERT, ref si)) return -1;
    if (si.nMax <= si.nMin || si.nPage > (uint)(si.nMax - si.nMin)) return -1;
    return si.nPos;
  }

  public static IntPtr Root(IntPtr tree) { return Next(tree, TVGN_ROOT, IntPtr.Zero); }
  public static IntPtr FirstVisible(IntPtr tree) { return Next(tree, TVGN_FIRSTVISIBLE, IntPtr.Zero); }
  public static IntPtr FirstChild(IntPtr tree, IntPtr item) { return Next(tree, TVGN_CHILD, item); }

  // Öğenin metni ve durum bitleri; uzak süreç belleği üzerinden TVM_GETITEMW.
  public static string ItemText(IntPtr tree, IntPtr item, out uint state) {
    state = 0;
    uint pid; GetWindowThreadProcessId(tree, out pid);
    IntPtr proc = OpenProcess(0x0008 | 0x0010 | 0x0020, false, pid); // VM_OPERATION|VM_READ|VM_WRITE
    if (proc == IntPtr.Zero) throw new Exception("OpenProcess failed");
    IntPtr mem = VirtualAllocEx(proc, IntPtr.Zero, 4096, 0x3000, 0x04);
    if (mem == IntPtr.Zero) { CloseHandle(proc); throw new Exception("VirtualAllocEx failed"); }
    try {
      // TVITEMW (x64): mask(0) pad(4) hItem(8) state(16) stateMask(20) pszText(24) cchTextMax(32) iImage(36) iSelectedImage(40) cChildren(44) lParam(48)
      byte[] tv = new byte[56];
      BitConverter.GetBytes(0x0001 | 0x0008).CopyTo(tv, 0); // TVIF_TEXT|TVIF_STATE
      BitConverter.GetBytes(item.ToInt64()).CopyTo(tv, 8);
      BitConverter.GetBytes(0xFFFF).CopyTo(tv, 20);          // stateMask: all
      BitConverter.GetBytes(mem.ToInt64() + 256).CopyTo(tv, 24);
      BitConverter.GetBytes(512).CopyTo(tv, 32);
      IntPtr n;
      if (!WriteProcessMemory(proc, mem, tv, tv.Length, out n)) throw new Exception("WriteProcessMemory failed");
      if (Send(tree, TVM_GETITEMW, IntPtr.Zero, mem) == IntPtr.Zero) throw new Exception("TVM_GETITEMW failed");
      if (!ReadProcessMemory(proc, mem, tv, tv.Length, out n)) throw new Exception("ReadProcessMemory failed");
      byte[] txt = new byte[1024];
      if (!ReadProcessMemory(proc, (IntPtr)(mem.ToInt64() + 256), txt, 1024, out n)) throw new Exception("ReadProcessMemory failed");
      string s = Encoding.Unicode.GetString(txt); int z = s.IndexOf('\0'); if (z >= 0) s = s.Substring(0, z);
      state = BitConverter.ToUInt32(tv, 16);
      return s;
    } finally { VirtualFreeEx(proc, mem, 0, 0x8000); CloseHandle(proc); }
  }

  // Gizli kökün (Desktop) çocukları: "tutamaç|ad|pinli" satırları, ağaçtaki sırayla.
  public static List<string> RootChildren(IntPtr tree) {
    var result = new List<string>();
    IntPtr root = Root(tree);
    if (root == IntPtr.Zero) return result;
    for (IntPtr item = FirstChild(tree, root); item != IntPtr.Zero; item = Next(tree, TVGN_NEXT, item)) {
      uint state; string s = ItemText(tree, item, out state);
      bool pinned = ((state >> 12) & 0xF) == 1;
      result.Add(item.ToInt64().ToString() + "|" + s + "|" + (pinned ? "1" : "0"));
    }
    return result;
  }
}
"@

$script:tree = [IntPtr]::Zero
$script:items = @()

function Show-Tree {
    if (-not $script:items.Count) { return }
    Write-Output 'tree (root children, in order):'
    $i = 0
    foreach ($it in $script:items) {
        $i++
        Write-Output ("  {0,2}. {1}{2}" -f $i, $(if ($it.pinned) { '[pin] ' } else { '' }), $it.name)
    }
}

function Name-Of([IntPtr]$item) {
    if ($item -eq [IntPtr]::Zero) { return '<none>' }
    $match = $script:items | Where-Object { $_.handle -eq $item.ToInt64() } | Select-Object -First 1
    if ($match) { return "'$($match.name)'" }
    $state = [uint32]0
    try { return "'$([NavTree]::ItemText($script:tree, $item, [ref]$state))'" } catch { return "0x$($item.ToInt64().ToString('X'))" }
}

function Fail([string]$reason) {
    Show-Tree
    Write-Output "FAIL: $reason"
    exit 1
}

try {
    if ($Hwnd) {
        $top = [IntPtr][long]$Hwnd
    }
    else {
        $cabinets = [NavTree]::FindCabinets()
        if ($cabinets.Count -eq 0) { Fail 'no visible File Explorer window found' }
        $top = $cabinets[0]
    }
    Write-Output "window: 0x$($top.ToInt64().ToString('X'))"

    $script:tree = [NavTree]::FindTree($top)
    if ($script:tree -eq [IntPtr]::Zero) { Fail 'navigation tree (SysTreeView32) not found' }
    $tree = $script:tree

    $script:items = @(foreach ($line in [NavTree]::RootChildren($tree)) {
        $parts = $line.Split('|')
        $name = ($parts[1..($parts.Count - 2)] -join '|')
        [pscustomobject]@{ handle = [long]$parts[0]; name = $name; pinned = ($parts[-1] -eq '1') }
    })
    $items = $script:items
    if ($items.Count -eq 0) { Fail 'navigation tree has no root children' }

    # (a) Pinli olmayanların ilk N tanesi -Expected ile birebir aynı mı?
    $unpinned = @($items | Where-Object { -not $_.pinned } | ForEach-Object { $_.name })
    for ($k = 0; $k -lt $Expected.Count; $k++) {
        if ($k -ge $unpinned.Count -or $unpinned[$k] -cne $Expected[$k]) {
            Fail "unpinned item #$($k + 1) expected '$($Expected[$k])'"
        }
    }
    if ($Exact -and $unpinned.Count -ne $Expected.Count) {
        Fail "-Exact: $($unpinned.Count) unpinned item(s), expected $($Expected.Count)"
    }

    # (b) Pinlilerin yeri
    $pinIdx = @(0..($items.Count - 1) | Where-Object { $items[$_].pinned })
    $unpinIdx = @(0..($items.Count - 1) | Where-Object { -not $items[$_].pinned })
    if ($pinIdx.Count -gt 0 -and $unpinIdx.Count -gt 0) {
        if ($Pinned -eq 'bottom' -and ($pinIdx | Measure-Object -Minimum).Minimum -lt ($unpinIdx | Measure-Object -Maximum).Maximum) {
            Fail 'pinned items are not all after unpinned ones'
        }
        if ($Pinned -eq 'top' -and ($pinIdx | Measure-Object -Maximum).Maximum -gt ($unpinIdx | Measure-Object -Minimum).Minimum) {
            Fail 'pinned items are not all before unpinned ones'
        }
    }

    # (c) Pin ikonları (state image list) kaldırılmış mı?
    if ($NoPinIcons) {
        $il = [NavTree]::StateImageList($tree)
        if ($il -ne 0) { Fail "state image list is still set (0x$($il.ToString('X')))" }
    }

    # (d) İlk görünür öğe: sıralamadan sonra eski ilk çocukta takılı kalırsa ağaç boş çizilir.
    $root = [NavTree]::Root($tree)
    $firstChild = [NavTree]::FirstChild($tree, $root)
    $firstVisible = [NavTree]::FirstVisible($tree)
    $pos = [NavTree]::ScrollPos($tree)
    $where = if ($pos -lt 0) { 'no scroll range' } else { "scroll position $pos" }
    Write-Output "first visible: $(Name-Of $firstVisible) ($where; root $(Name-Of $root), first child $(Name-Of $firstChild))"
    if ($pos -le 0) {
        if ($firstVisible -ne $root -and $firstVisible -ne $firstChild) {
            Fail "first visible item is $(Name-Of $firstVisible), expected the root or its first child $(Name-Of $firstChild) ($where)"
        }
    }
    else {
        Write-Output 'first visible check skipped: the tree is scrolled'
    }

    Show-Tree
    Write-Output 'PASS'
    exit 0
}
catch {
    Fail "exception: $($_.Exception.Message)"
}
