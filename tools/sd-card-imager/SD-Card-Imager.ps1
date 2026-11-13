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

    // An uncompressed image file that skips empty space on disk (NTFS sparse file)
    public static void SetSparse(FileStream f) {
        uint r;
        DeviceIoControl(f.SafeFileHandle, 0x000900C4, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero);   // FSCTL_SET_SPARSE
    }
}

// Which parts of a FAT32 card hold anything. Everything up to the end of the FAT tables is
// always copied; after that, only clusters the FAT marks as used. Empty clusters are left out
// (zeros in the image, skipped on the card). Anything that isn't plain FAT32 is copied whole.
public class FatMap {
    public long PartStart, PartEnd, FatStart, DataStart;
    public int ClusterSize;
    public long ClusterCount;
    public bool OtherPartitions;
    uint[] fat;

    static uint U32(byte[] b, long o) { return (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)); }
    static int U16(byte[] b, long o) { return b[o] | (b[o + 1] << 8); }
    static bool IsFat32Boot(byte[] b, long o, long len) {
        if (o + 512 > len) return false;
        if (b[o + 510] != 0x55 || b[o + 511] != 0xAA) return false;
        return b[o + 82] == (byte)'F' && b[o + 83] == (byte)'A' && b[o + 84] == (byte)'T' && b[o + 85] == (byte)'3' && b[o + 86] == (byte)'2';
    }

    // How many bytes from the start of the card are needed to read the map: -1 = not FAT32 (copy
    // everything); a number bigger than len = read that much first; otherwise the map is ready.
    public static long BytesNeeded(byte[] b, long len, out long part, out bool others) {
        part = -1; others = false;
        if (len < 512) return 512;
        if (b[510] != 0x55 || b[511] != 0xAA) return -1;
        if (IsFat32Boot(b, 0, len)) part = 0;                        // no partition table
        else {
            for (int i = 0; i < 4; i++) {
                long e = 446 + 16 * i;
                byte type = b[e + 4];
                if (type == 0) continue;
                if (part < 0 && (type == 0x0B || type == 0x0C)) part = (long)U32(b, e + 8) * 512;
                else others = true;
            }
            if (part < 0) return -1;
        }
        if (len < part + 512) return part + 512;
        if (!IsFat32Boot(b, part, len)) return -1;
        int bps = U16(b, part + 11), spc = b[part + 13], rsvd = U16(b, part + 14), nf = b[part + 16];
        uint fatsz = U32(b, part + 36);
        if (bps < 512 || bps > 4096 || spc == 0 || nf == 0 || fatsz == 0) return -1;
        return part + ((long)rsvd + (long)nf * fatsz) * bps;
    }

    public static FatMap Build(byte[] b, long len) {
        long part; bool others;
        long need = BytesNeeded(b, len, out part, out others);
        if (need < 0 || need > len) return null;
        FatMap m = new FatMap();
        int bps = U16(b, part + 11), spc = b[part + 13], rsvd = U16(b, part + 14), nf = b[part + 16];
        uint fatsz = U32(b, part + 36), tot = U32(b, part + 32);
        m.PartStart = part; m.OtherPartitions = others;
        m.PartEnd = part + (long)tot * bps;
        m.FatStart = part + (long)rsvd * bps;
        m.DataStart = need;
        m.ClusterSize = bps * spc;
        m.ClusterCount = ((long)tot - rsvd - (long)nf * fatsz) / spc;
        long entries = Math.Min((long)fatsz * bps / 4, m.ClusterCount + 2);
        m.fat = new uint[entries];
        for (long i = 0; i < entries; i++) m.fat[i] = U32(b, m.FatStart + i * 4) & 0x0FFFFFFF;
        return m;
    }

    // Reads the map straight from a card (from its first bytes); null if it isn't FAT32.
    public static FatMap Probe(Stream s, long size) {
        byte[] b = new byte[1 << 20]; long len = 0;
        for (;;) {
            long part; bool others;
            long need = BytesNeeded(b, len, out part, out others);
            if (need < 0 || need > size || need > (1L << 30)) return null;
            if (need <= len) return Build(b, len);
            long want = (need + 4095) & ~4095L;
            if (want > b.Length) { byte[] nb = new byte[Math.Max(want, (long)b.Length * 2)]; Array.Copy(b, nb, len); b = nb; }
            s.Seek(len, SeekOrigin.Begin);
            while (len < want) { int n = s.Read(b, (int)len, (int)(want - len)); if (n <= 0) return null; len += n; }
        }
    }

    public long UsedBytes() {
        long used = 0;
        for (long c = 2; c < fat.Length; c++) if (fat[c] != 0) used += ClusterSize;
        return used + DataStart;
    }

    // Does this stretch of the card hold anything?
    public bool Needed(long off, long len) {
        long end = off + len;
        if (off < DataStart) return true;
        if (off >= PartEnd) return OtherPartitions;                  // past the partition
        if (end > PartEnd && OtherPartitions) return true;
        long last = Math.Min(end, PartEnd) - 1;
        long c0 = 2 + (off - DataStart) / ClusterSize, c1 = 2 + (last - DataStart) / ClusterSize;
        for (long c = c0; c <= c1; c++) {
            if (c >= fat.Length) break;                              // slack after the last cluster
            if (fat[c] != 0) return true;
        }
        return false;
    }
}

// The start of an image as it streams past, until there's enough to read its map.
public class HeadCollector {
    MemoryStream ms = new MemoryStream();
    public FatMap Map;
    public bool Done;
    public void Add(byte[] b, int n) {
        if (Done) return;
        ms.Write(b, 0, n);
        byte[] buf = ms.GetBuffer(); long len = ms.Length;
        long part; bool others;
        long need = FatMap.BytesNeeded(buf, len, out part, out others);
        if (need < 0 || len > (1L << 30)) { Done = true; ms = null; return; }
        if (need <= len) { Map = FatMap.Build(buf, len); Done = true; ms = null; }
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
$form.ClientSize = New-Object Drawing.Size(560, 350)
$form.AutoScaleDimensions = New-Object Drawing.SizeF(96, 96); $form.AutoScaleMode = 'Dpi'
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

$quick = New-Object Windows.Forms.CheckBox
$quick.Text = 'Quick: only the parts in use (FAT32 cards)'
$quick.Checked = $true; $quick.Location = New-Object Drawing.Point(16, 272); $quick.AutoSize = $true
$form.Controls.Add($quick)

$showAll = New-Object Windows.Forms.CheckBox
$showAll.Text = 'Show all drives'
$showAll.Location = New-Object Drawing.Point(16, 300); $showAll.AutoSize = $true
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
$status.Location = New-Object Drawing.Point(16, 208); $status.Size = New-Object Drawing.Size(528, 56)
$form.Controls.Add($status)

$cancelBtn = New-Object Windows.Forms.Button
$cancelBtn.Text = 'Stop'; $cancelBtn.Location = New-Object Drawing.Point(432, 290); $cancelBtn.Size = New-Object Drawing.Size(112, 40); $cancelBtn.Enabled = $false
$form.Controls.Add($cancelBtn)

$script:cards = @()
$script:stop = $false
$script:busy = $false

function Load-Cards([bool]$keepStatus = $false) {
    $combo.Items.Clear()
    $script:cards = @(Get-Cards)
    foreach ($c in $script:cards) { [void]$combo.Items.Add($c.Label) }
    if ($script:cards.Count) { $combo.SelectedIndex = 0; if (-not $keepStatus) { $status.Text = 'Pick the card, then save it or write an image to it.' } }
    else { $status.Text = "No card found. Windows sees: $(Disk-Summary). If the card isn't there, try another reader or slot; if it is, tick 'Show all drives'." }
}

function Set-Busy([bool]$on) {
    $script:busy = $on
    $saveBtn.Enabled = -not $on; $writeBtn.Enabled = -not $on; $refresh.Enabled = -not $on; $combo.Enabled = -not $on; $compress.Enabled = -not $on; $showAll.Enabled = -not $on; $quick.Enabled = -not $on
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
        $map = $null
        if ($quick.Checked) {
            $status.Text = 'Reading the card''s map of used space...'; [Windows.Forms.Application]::DoEvents()
            $map = [FatMap]::Probe($disk, [int64]$card.Size)
            [void]$disk.Seek(0, 'Begin')
        }
        $out = [IO.File]::Create($outPath)
        $dest = $out
        if ($compress.Checked) { $gz = New-Object IO.Compression.GZipStream($out, [IO.Compression.CompressionLevel]::Fastest); $dest = $gz }
        else { [RawDisk]::SetSparse($out) }
        $buf = New-Object byte[] $CHUNK
        $zero = New-Object byte[] $CHUNK
        [int64]$done = 0
        $verb = if ($map) { "Saving (only the $(Format-Size $map.UsedBytes()) in use)" } else { 'Saving' }
        while ($done -lt [int64]$card.Size -and -not $script:stop) {
            $want = [int][Math]::Min([int64]$CHUNK, [int64]$card.Size - $done)   # (both as 64-bit: card sizes don't fit 32)
            if ($map -and -not $map.Needed($done, $want)) {
                # empty space: nothing to read; zeros in the image (a hole in an uncompressed one)
                [void]$disk.Seek($want, 'Current')
                if ($gz) { $gz.Write($zero, 0, $want) } else { [void]$out.Seek($want, 'Current') }
                $n = $want
            } else {
                $n = Read-Full $disk $buf $want
                if ($n -le 0) { throw "The card stopped answering after $(Format-Size $done)." }
                $dest.Write($buf, 0, $n)
            }
            $done += $n
            Show-Progress $done ([int64]$card.Size) $sw $verb
        }
        if (-not $gz) { $out.SetLength([int64]$card.Size) }
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
        $status.Text = "Complete. Saved $outPath  ($(Format-Size $size), in $(Format-Time $sw.Elapsed.TotalSeconds))"
        $bar.Value = 0
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
        $head = New-Object HeadCollector
        if (-not $quick.Checked) { $head.Done = $true }
        while (-not $script:stop) {
            $n = Read-Full $src $buf $CHUNK
            if ($n -le 0) { break }
            $pad = ($n % 512)
            if ($pad) { [Array]::Clear($buf, $n, 512 - $pad); $n += 512 - $pad }   # cards take whole sectors
            $head.Add($buf, $n)
            if ($head.Map -and -not $head.Map.Needed($done, $n)) {
                # empty space in the image: skip it on the card too
                if ($done + $n -le [int64]$card.Size) { [void]$disk.Seek($n, 'Current') }
            } else {
                if ($done + $n -gt [int64]$card.Size) { throw "The image is bigger than this card; it doesn't fit." }
                $disk.Write($buf, 0, $n)
            }
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
        $status.Text = "Complete. Written in $(Format-Time $sw.Elapsed.TotalSeconds). Take the card out and put it back in before using it."
        $bar.Value = 0
    } else {
        $status.Text = 'Stopped part way: the card is incomplete. Write the image again before using it.'; $bar.Value = 0
    }
    Set-Busy $false
    Load-Cards $true
})

Load-Cards
[void]$form.ShowDialog()
