# HKCU\Software\Classes\CLSID\<guid> altındaki System.IsPinnedToNameSpaceTree değerini SALT OKUNUR okuyup
# beklenenle karşılaştırır. Registry'ye hiçbir şey yazmaz.
# Kullanım: powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-hidden.ps1 `
#             -Clsid '{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}' -Expect 0
#           -Expect: 'absent' (değer yok) ya da beklenen sayısal değer (ör. 0, 1).
# Çıktı: "observed: <değer>" ve PASS (çıkış kodu 0) ya da FAIL: <neden> (çıkış kodu 1).
param(
    [Parameter(Mandatory = $true)][string]$Clsid,
    [Parameter(Mandatory = $true)][string]$Expect
)

if ($Clsid -notmatch '^\{[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\}$') {
    Write-Host "FAIL: -Clsid is not a {GUID}: $Clsid"
    exit 1
}
if ($Expect -ne 'absent' -and $Expect -notmatch '^\d+$') {
    Write-Host "FAIL: -Expect must be 'absent' or a decimal number: $Expect"
    exit 1
}

$valueName = 'System.IsPinnedToNameSpaceTree'
$observed = 'absent'
# OpenSubKey varsayılan olarak salt okunurdur.
$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Classes\CLSID\$Clsid")
if ($key) {
    try {
        $value = $key.GetValue($valueName, $null)
        if ($null -ne $value) {
            $kind = $key.GetValueKind($valueName)
            $observed = if ($kind -eq 'DWord') { [string][uint32][int64]$value } else { "$value (kind=$kind)" }
        }
    }
    finally { $key.Close() }
}

Write-Host "observed: $observed"
if ($observed -eq $Expect) {
    Write-Host 'PASS'
    exit 0
}
Write-Host "FAIL: expected $Expect, observed $observed"
exit 1
