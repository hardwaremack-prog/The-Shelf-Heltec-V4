# SD Card Imager - save a whole SD card to an image file, and write an image back onto a card.
# Windows 10/11, nothing to install. Start it with "SD Card Imager.cmd" (it asks for admin rights,
# which Windows needs for reading or writing a whole card).
#
# Only SD cards and USB drives are listed (never the computer's own disks). Images are saved as
# .img (exact copy) or .img.gz (compressed: empty space shrinks to almost nothing).

# ---- run as administrator (reading a whole disk needs it)
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSCommandPath`""
    exit
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class RawDisk {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(SafeFileHandle h, uint code, IntPtr inBuf, uint inSize, IntPtr outBuf, uint outSize, out uint returned, IntPtr overlapped);

    const uint GENERIC_READ = 0x80000000, GENERIC_WRITE = 0x40000000, SHARE_READ_WRITE = 3, OPEN_EXISTING = 3;
    const uint FSCTL_LOCK_VOLUME = 0x00090018, FSCTL_DISMOUNT_VOLUME = 0x00090020;

    public static FileStream OpenDisk(int number, bool write) {
        SafeFileHandle h = CreateFile(@"\\.\PhysicalDrive" + number, write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                                      SHARE_READ_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new IOException("Windows wouldn't open the card (error " + Marshal.GetLastWin32Error() + ").");
        return new FileStream(h, write ? FileAccess.ReadWrite : FileAccess.Read, 4096, false);
    }

    // Takes a volume on the card away from Windows while it's being written (so nothing else
    // writes to it at the same time). Keep the handle until the writing is done.
    public static SafeFileHandle LockVolume(string path) {
        SafeFileHandle h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, SHARE_READ_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new IOException("Windows wouldn't let go of the card (error " + Marshal.GetLastWin32Error() + ").");
        uint r;
        if (!DeviceIoControl(h, FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero)) {
            h.Dispose();
            throw new IOException("The card is in use. Close any windows or programs showing files on it, then try again.");
        }
        DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero);
        return h;
    }
}
'@

$CHUNK = 4MB      # a multiple of every card's sector size

function Format-Size([double]$b) {
    if ($b -ge 1GB) { '{0:N1} GB' -f ($b / 1GB) } elseif ($b -ge 1MB) { '{0:N0} MB' -f ($b / 1MB) } else { '{0:N0} KB' -f ($b / 1KB) }
}
function Format-Time([double]$s) {
    if ($s -lt 60) { '{0:N0} s' -f $s } elseif ($s -lt 3600) { '{0:N0} min' -f ($s / 60) } else { '{0:N1} h' -f ($s / 3600) }
}

# The kind of connection Windows reports for a disk (it can come back as a word or a number)
function Bus-Name($d) {
    $b = "$($d.BusType)"
    switch ($b) { '7' { 'USB' } '12' { 'SD' } '13' { 'MMC' } '1' { 'SCSI' } '3' { 'ATA' } '11' { 'SATA' } '17' { 'NVMe' } '8' { 'RAID' } default { $b } }
}

# SD cards and USB drives, never the disk Windows runs from. Card readers built into a computer
# often report themselves as SCSI or ATA rather than SD, so "removable media" counts too.
# "Show all drives" lists every disk except Windows' own.
function Get-Cards {
    $removable = @{}
    foreach ($w in @(Get-CimInstance Win32_DiskDrive -ErrorAction SilentlyContinue)) {
        if ("$($w.MediaType)" -match 'Removable|External' -or "$($w.InterfaceType)" -eq 'USB' -or "$($w.PNPDeviceID)" -match 'SD|MMC|CARD|USBSTOR') { $removable[[int]$w.Index] = $true }
    }
    $list = @()
    foreach ($d in @(Get-Disk -ErrorAction SilentlyContinue)) {
        if ($d.IsBoot -or $d.IsSystem -or -not $d.Size -or $d.Size -le 0) { continue }
        $bus = Bus-Name $d
        $isCard = ($bus -in @('SD', 'USB', 'MMC')) -or $removable.ContainsKey([int]$d.Number)
        if (-not $isCard -and -not $showAll.Checked) { continue }
        if ($d.Size -gt 2TB -and -not $showAll.Checked) { continue }
        $letters = @(Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue | Where-Object { $_.DriveLetter } | ForEach-Object { "$($_.DriveLetter):" })
        $name = ("$($d.FriendlyName)" -replace '\s+', ' ').Trim()
        $label = "Disk $($d.Number): $name, $(Format-Size $d.Size), $bus" + $(if ($letters.Count) { "  ($($letters -join ' '))" } else { '' })
        $list += [pscustomobject]@{ Number = [int]$d.Number; Size = [int64]$d.Size; Label = $label; Letters = $letters }
    }
    $list
}

# What Windows sees, for when no card shows up
function Disk-Summary {
    $parts = foreach ($d in @(Get-Disk -ErrorAction SilentlyContinue)) {
        $tag = if ($d.IsBoot -or $d.IsSystem) { ', Windows' } else { '' }
        "Disk $($d.Number) $(Bus-Name $d) $(Format-Size $d.Size)$tag"
    }
    if ($parts) { $parts -join ' | ' } else { 'no disks at all (is this running as administrator?)' }
}

# ---- window
$form = New-Object Windows.Forms.Form
$form.Text = 'SD Card Imager'
$form.ClientSize = New-Object Drawing.Size(560, 300)
$form.FormBorderStyle = 'FixedSingle'; $form.MaximizeBox = $false
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object Drawing.Font('Segoe UI', 10)

$title = New-Object Windows.Forms.Label
$title.Text = 'Save an SD card to an image file, or write an image onto a card.'
$title.Location = New-Object Drawing.Point(16, 14); $title.AutoSize = $true
$form.Controls.Add($title)

$lbl = New-Object Windows.Forms.Label
$lbl.Text = 'Card:'; $lbl.Location = New-Object Drawing.Point(16, 52); $lbl.AutoSize = $true
$form.Controls.Add($lbl)

$combo = New-Object Windows.Forms.ComboBox
$combo.DropDownStyle = 'DropDownList'
$combo.Location = New-Object Drawing.Point(64, 48); $combo.Size = New-Object Drawing.Size(380, 28)
$form.Controls.Add($combo)

$refresh = New-Object Windows.Forms.Button
$refresh.Text = 'Refresh'; $refresh.Location = New-Object Drawing.Point(452, 47); $refresh.Size = New-Object Drawing.Size(92, 30)
$form.Controls.Add($refresh)

$compress = New-Object Windows.Forms.CheckBox
$compress.Text = 'Compress the image (.img.gz: much smaller, empty space takes almost nothing)'
$compress.Checked = $true; $compress.Location = New-Object Drawing.Point(16, 88); $compress.AutoSize = $true
$form.Controls.Add($compress)

$showAll = New-Object Windows.Forms.CheckBox
$showAll.Text = 'Show all drives'
$showAll.Location = New-Object Drawing.Point(16, 266); $showAll.AutoSize = $true
$form.Controls.Add($showAll)
$showAll.Add_CheckedChanged({ Load-Cards })

$saveBtn = New-Object Windows.Forms.Button
$saveBtn.Text = 'Save card to image...'; $saveBtn.Location = New-Object Drawing.Point(16, 124); $saveBtn.Size = New-Object Drawing.Size(260, 40)
$form.Controls.Add($saveBtn)

$writeBtn = New-Object Windows.Forms.Button
$writeBtn.Text = 'Write image to card...'; $writeBtn.Location = New-Object Drawing.Point(284, 124); $writeBtn.Size = New-Object Drawing.Size(260, 40)
$form.Controls.Add($writeBtn)

$bar = New-Object Windows.Forms.ProgressBar
$bar.Location = New-Object Drawing.Point(16, 180); $bar.Size = New-Object Drawing.Size(528, 22); $bar.Maximum = 1000
$form.Controls.Add($bar)

$status = New-Object Windows.Forms.Label
$status.Location = New-Object Drawing.Point(16, 208); $status.Size = New-Object Drawing.Size(528, 58)
$form.Controls.Add($status)

$cancelBtn = New-Object Windows.Forms.Button
$cancelBtn.Text = 'Stop'; $cancelBtn.Location = New-Object Drawing.Point(452, 258); $cancelBtn.Size = New-Object Drawing.Size(92, 30); $cancelBtn.Enabled = $false
$form.Controls.Add($cancelBtn)

$script:cards = @()
$script:stop = $false
$script:busy = $false

function Load-Cards {
    $combo.Items.Clear()
    $script:cards = @(Get-Cards)
    foreach ($c in $script:cards) { [void]$combo.Items.Add($c.Label) }
    if ($script:cards.Count) { $combo.SelectedIndex = 0; $status.Text = 'Pick the card, then save it or write an image to it.' }
    else { $status.Text = "No card found. Windows sees: $(Disk-Summary). If the card isn't there, try another reader or slot; if it is, tick 'Show all drives'." }
}

function Set-Busy([bool]$on) {
    $script:busy = $on
    $saveBtn.Enabled = -not $on; $writeBtn.Enabled = -not $on; $refresh.Enabled = -not $on; $combo.Enabled = -not $on; $compress.Enabled = -not $on; $showAll.Enabled = -not $on
    $cancelBtn.Enabled = $on
}

function Show-Progress([int64]$done, [int64]$total, $sw, [string]$verb) {
    $bar.Value = [Math]::Min(1000, [int](1000.0 * $done / [Math]::Max([double]1, [double]$total)))
    $secs = [Math]::Max(0.1, $sw.Elapsed.TotalSeconds)
    $speed = $done / $secs
    $left = if ($speed -gt 0) { ($total - $done) / $speed } else { 0 }
    $status.Text = "$verb $(Format-Size $done) of $(Format-Size $total)  -  $(Format-Size $speed)/s  -  about $(Format-Time $left) left"
    [Windows.Forms.Application]::DoEvents()
}

function Selected-Card {
    if ($combo.SelectedIndex -lt 0) { return $null }
    $script:cards[$combo.SelectedIndex]
}

# Reads as much as asked for (streams can return less than asked)
function Read-Full($stream, [byte[]]$buf, [int]$want) {
    $got = 0
    while ($got -lt $want) {
        $n = $stream.Read($buf, $got, $want - $got)
        if ($n -le 0) { break }
        $got += $n
    }
    $got
}

$refresh.Add_Click({ Load-Cards })
$cancelBtn.Add_Click({ $script:stop = $true; $status.Text = 'Stopping...' })
$form.Add_FormClosing({ param($s, $e) if ($script:busy) { $script:stop = $true; $e.Cancel = $true } })

# ---- card -> image
$saveBtn.Add_Click({
    $card = Selected-Card
    if (-not $card) { return }
    $dlg = New-Object Windows.Forms.SaveFileDialog
    $ext = if ($compress.Checked) { 'img.gz' } else { 'img' }
    $dlg.FileName = "sd-card-$(Get-Date -Format yyyy-MM-dd).$ext"
    $dlg.Filter = if ($compress.Checked) { 'Compressed card image (*.img.gz)|*.img.gz' } else { 'Card image (*.img)|*.img' }
    $dlg.InitialDirectory = [Environment]::GetFolderPath('MyDocuments')
    if ($dlg.ShowDialog() -ne 'OK') { return }
    $outPath = $dlg.FileName
    $drive = [IO.Path]::GetPathRoot($outPath)
    try { $free = (New-Object IO.DriveInfo($drive)).AvailableFreeSpace } catch { $free = -1 }
    if (-not $compress.Checked -and $free -ge 0 -and $free -lt $card.Size) {
        [void][Windows.Forms.MessageBox]::Show("There isn't room on $drive for a $(Format-Size $card.Size) image. Tick 'Compress', or pick another drive.", 'SD Card Imager'); return
    }
    Set-Busy $true; $script:stop = $false
    $disk = $null; $out = $null; $gz = $null; $ok = $false
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $disk = [RawDisk]::OpenDisk($card.Number, $false)
        $out = [IO.File]::Create($outPath)
        $dest = $out
        if ($compress.Checked) { $gz = New-Object IO.Compression.GZipStream($out, [IO.Compression.CompressionLevel]::Fastest); $dest = $gz }
        $buf = New-Object byte[] $CHUNK
        [int64]$done = 0
        while ($done -lt $card.Size -and -not $script:stop) {
            $want = [int][Math]::Min([int64]$CHUNK, [int64]$card.Size - $done)   # (both as 64-bit: card sizes don't fit 32)
            $n = Read-Full $disk $buf $want
            if ($n -le 0) { throw "The card stopped answering after $(Format-Size $done)." }
            $dest.Write($buf, 0, $n)
            $done += $n
            Show-Progress $done $card.Size $sw 'Saving'
        }
        $ok = -not $script:stop
    } catch {
        [void][Windows.Forms.MessageBox]::Show("Saving stopped: $($_.Exception.Message)", 'SD Card Imager')
    } finally {
        if ($gz) { $gz.Dispose() }
        if ($out) { $out.Dispose() }
        if ($disk) { $disk.Dispose() }
    }
    if ($ok) {
        $size = (Get-Item $outPath).Length
        $status.Text = "Saved: $outPath  ($(Format-Size $size), in $(Format-Time $sw.Elapsed.TotalSeconds))"
        $bar.Value = 1000
    } else {
        Remove-Item $outPath -ErrorAction SilentlyContinue
        $status.Text = 'Stopped. The unfinished image was deleted.'; $bar.Value = 0
    }
    Set-Busy $false
})

# ---- image -> card
$writeBtn.Add_Click({
    $card = Selected-Card
    if (-not $card) { return }
    $dlg = New-Object Windows.Forms.OpenFileDialog
    $dlg.Filter = 'Card images (*.img;*.img.gz)|*.img;*.img.gz|All files (*.*)|*.*'
    $dlg.InitialDirectory = [Environment]::GetFolderPath('MyDocuments')
    if ($dlg.ShowDialog() -ne 'OK') { return }
    $inPath = $dlg.FileName
    $zipped = $inPath.ToLower().EndsWith('.gz')
    $fileSize = (Get-Item $inPath).Length
    if (-not $zipped -and $fileSize -gt $card.Size) {
        [void][Windows.Forms.MessageBox]::Show("That image ($(Format-Size $fileSize)) is bigger than this card ($(Format-Size $card.Size)). Use a card at least as big as the one it came from.", 'SD Card Imager'); return
    }
    $msg = "Everything on this card will be replaced by the image:`n`n    $($card.Label)`n`nMake sure it's the right card. Continue?"
    if ($card.Size -gt 300GB) { $msg = "This drive is big for an SD card:`n`n    $($card.Label)`n`nEverything on it will be replaced. Are you SURE it's the card?" }
    if ([Windows.Forms.MessageBox]::Show($msg, 'SD Card Imager', 'YesNo', 'Warning') -ne 'Yes') { return }

    Set-Busy $true; $script:stop = $false
    $locks = @(); $disk = $null; $in = $null; $gz = $null; $ok = $false
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $status.Text = 'Getting the card ready...'; [Windows.Forms.Application]::DoEvents()
        foreach ($p in @(Get-Partition -DiskNumber $card.Number -ErrorAction SilentlyContinue)) {
            $v = $p | Get-Volume -ErrorAction SilentlyContinue
            if ($v -and $v.Path) { $locks += [RawDisk]::LockVolume(($v.Path.TrimEnd('\') -replace '^\\\\\?\\', '\\.\')) }
        }
        $disk = [RawDisk]::OpenDisk($card.Number, $true)
        $in = [IO.File]::OpenRead($inPath)
        $src = $in
        if ($zipped) { $gz = New-Object IO.Compression.GZipStream($in, [IO.Compression.CompressionMode]::Decompress); $src = $gz }
        $buf = New-Object byte[] $CHUNK
        [int64]$done = 0
        $total = if ($zipped) { $card.Size } else { $fileSize }
        while (-not $script:stop) {
            $n = Read-Full $src $buf $CHUNK
            if ($n -le 0) { break }
            if ($done + $n -gt [int64]$card.Size) { throw "The image is bigger than this card; it doesn't fit." }
            $pad = ($n % 512)
            if ($pad) { [Array]::Clear($buf, $n, 512 - $pad); $n += 512 - $pad }   # cards take whole sectors
            $disk.Write($buf, 0, $n)
            $done += $n
            Show-Progress ($(if ($zipped) { $in.Position } else { $done })) ($(if ($zipped) { $fileSize } else { $total })) $sw 'Writing'
        }
        $disk.Flush()
        $ok = -not $script:stop
    } catch {
        [void][Windows.Forms.MessageBox]::Show("Writing stopped: $($_.Exception.Message)", 'SD Card Imager')
    } finally {
        if ($gz) { $gz.Dispose() }
        if ($in) { $in.Dispose() }
        if ($disk) { $disk.Dispose() }
        foreach ($l in $locks) { $l.Dispose() }
    }
    try { Update-Disk -Number $card.Number -ErrorAction SilentlyContinue } catch {}
    if ($ok) {
        $status.Text = "Written in $(Format-Time $sw.Elapsed.TotalSeconds). Take the card out and put it back in before using it."
        $bar.Value = 1000
    } else {
        $status.Text = 'Stopped part way: the card is incomplete. Write the image again before using it.'; $bar.Value = 0
    }
    Set-Busy $false
    Load-Cards
})

Load-Cards
[void]$form.ShowDialog()
