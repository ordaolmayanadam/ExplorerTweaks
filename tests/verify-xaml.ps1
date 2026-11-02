# Explorer pencerelerinin XAML üst bölümünü (DesktopChildSiteBridge altı) UIA ile SALT OKUNUR
# okur: tab satırı ve toolbar görünüyor mu, title bar (caption/sürükleme) alanı adres satırıyla
# çakışıyor mu? Hiçbir pencereye mesaj göndermez, hiçbir şeyi değiştirmez.
# Kullanım: powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-xaml.ps1 [-Check All|Tabs|CommandBar]
#   -Check Tabs       : yalnızca tab öğeleri (hideTabs açık, hideCommandBar kapalıyken)
#   -Check CommandBar : yalnızca toolbar (hideCommandBar açık, hideTabs kapalıyken)
#   -Check All        : ikisi (varsayılan)
# Çakışma kontrolü her modda yapılır. Simge durumundaki pencereler atlanır.
# Çıktı: pencere başına bulgular ve PASS (çıkış kodu 0) ya da FAIL: <neden> (çıkış kodu 1).
#
# Kimlikler (Step 1 keşfi, stok Explorer, Windows 11 26300, WinUI 3 / Windows App SDK):
#   CabinetWClass
#     TITLE_BAR_SCAFFOLDING_WINDOW_CLASS          (Win32; caption düğmeleri + sürükleme alanı)
#     Microsoft.UI.Content.DesktopChildSiteBridge (Win32; XAML island)
#       InputSiteWindowClass (fw=XAML)
#         [Tab]      class=Microsoft.UI.Xaml.Controls.TabView  aid=TabView
#           [List]     class=ListView      aid=TabListView
#             [TabItem]  class=ListViewItem  name=<klasör>
#           [Button]   class=Button        aid=AddButton
#         [-]        class=ApplicationBar  aid=NavigationCommands (geri/ileri/yukarı/yenile)
#         [Group]    class=AutoSuggestBox  aid=PART_AutoSuggestBox (adres çubuğu)
#         [Group]    class=AutoSuggestBox  aid=FileExplorerSearchBox
#         [-]        class=ApplicationBar  aid=FileExplorerCommandBar
#         [-]        class=ApplicationBar  aid=FileExplorerSecondaryCommandBar
# Mod tab satırını gizlerken TabView'ın kendisini (boş title bar şeridi olarak) bırakır; bu
# yüzden TabView değil, içindeki sekme öğeleri kontrol edilir.
param(
    [ValidateSet('All', 'Tabs', 'CommandBar')][string]$Check = 'All'
)

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$walker = [System.Windows.Automation.TreeWalker]::RawViewWalker

$tabAids = @('TabListView', 'AddButton')
$commandBarAids = @('FileExplorerCommandBar', 'FileExplorerSecondaryCommandBar')
$addressAids = @('NavigationCommands', 'PART_AutoSuggestBox', 'FileExplorerSearchBox')

function Get-Descendants($el, $depth) {
    if ($depth -gt 12) { return }
    $child = $walker.GetFirstChild($el)
    while ($child) {
        $child
        Get-Descendants $child ($depth + 1)
        $child = $walker.GetNextSibling($child)
    }
}

function Describe($el) {
    $c = $el.Current
    "[$($c.ControlType.ProgrammaticName)] class='$($c.ClassName)' aid='$($c.AutomationId)' name='$($c.Name)' rect=$($c.BoundingRectangle)"
}

$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ClassNameProperty, 'CabinetWClass')
$windows = $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)

$failures = @()
$checked = 0
foreach ($win in $windows) {
    $wc = $win.Current
    if ($wc.BoundingRectangle.IsEmpty -or $wc.BoundingRectangle.X -le -30000) { continue }  # simge durumunda
    $checked++
    Write-Host "window: '$($wc.Name)' hwnd=0x$('{0:X}' -f $wc.NativeWindowHandle) rect=$($wc.BoundingRectangle)"

    $scaffold = $null
    $bridge = $null
    $child = $walker.GetFirstChild($win)
    while ($child) {
        switch ($child.Current.ClassName) {
            'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' { $scaffold = $child }
            'Microsoft.UI.Content.DesktopChildSiteBridge' { $bridge = $child }
        }
        $child = $walker.GetNextSibling($child)
    }
    if (-not $bridge) {
        $failures += "'$($wc.Name)': no XAML island (DesktopChildSiteBridge)"
        continue
    }

    $visibleTabs = @()
    $visibleBars = @()
    $address = @()
    foreach ($el in (Get-Descendants $bridge 0)) {
        $c = $el.Current
        if ($c.IsOffscreen -or $c.BoundingRectangle.IsEmpty) { continue }
        $isTab = ($tabAids -contains $c.AutomationId) -or
                 ($c.ControlType -eq [System.Windows.Automation.ControlType]::TabItem)
        if ($isTab) { $visibleTabs += $el }
        if ($commandBarAids -contains $c.AutomationId) { $visibleBars += $el }
        if ($addressAids -contains $c.AutomationId) { $address += $el }
    }

    foreach ($el in $visibleTabs) { Write-Host "  visible tab element: $(Describe $el)" }
    foreach ($el in $visibleBars) { Write-Host "  visible command bar: $(Describe $el)" }
    if ($Check -in 'All', 'Tabs' -and $visibleTabs.Count) {
        $failures += "'$($wc.Name)': $($visibleTabs.Count) tab element(s) visible"
    }
    if ($Check -in 'All', 'CommandBar' -and $visibleBars.Count) {
        $failures += "'$($wc.Name)': $($visibleBars.Count) command bar(s) visible"
    }

    # Caption düğmeleri ve sürükleme alanı (scaffolding penceresi) adres satırına binmemeli.
    if ($scaffold) {
        $sr = $scaffold.Current.BoundingRectangle
        Write-Host "  title bar scaffolding rect=$sr"
        foreach ($el in $address) {
            $ar = $el.Current.BoundingRectangle
            $overlap = [System.Windows.Rect]::Intersect($sr, $ar)
            if (-not $overlap.IsEmpty -and $overlap.Width -gt 0 -and $overlap.Height -gt 0) {
                $failures += "'$($wc.Name)': title bar area overlaps $($el.Current.AutomationId) ($ar)"
            }
        }
    }
    else {
        Write-Host '  title bar scaffolding: not found (overlap check skipped)'
    }
}

if ($checked -eq 0) {
    Write-Host 'FAIL: no visible File Explorer window (open one first)'
    exit 1
}
if ($failures.Count) {
    foreach ($f in $failures) { Write-Host "FAIL: $f" }
    exit 1
}
Write-Host 'PASS'
exit 0
