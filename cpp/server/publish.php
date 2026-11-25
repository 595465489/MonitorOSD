<?php
/**
 * MonitorOSD 版本清单生成器 —— 宝塔「计划任务」里挂一个 PHP 脚本或用命令行跑
 *
 * 用法：
 *   php publish.php --channel=latest --dir=/www/wwwroot/osd.example.com/api/pkgs --url=https://osd.example.com/api/pkgs
 *
 * 它会扫描 $dir 下的 MonitorOSD_<version>.exe，取版本号最高的那个，
 * 生成 / 覆盖同目录（$dir/../）的 <channel>.json：
 *   {"version":"1.3.1","url":".../MonitorOSD_1.3.1.exe","sha256":"...","size":123456,
 *    "force":false,"notes":["..."]}
 *
 * 说明：
 *   - sha256 / size 自动算，杜绝手抖写错导致客户端拒绝安装
 *   - notes 从同名的 .notes.txt 读取（一行一条）；没有就留空数组
 *   - force=true 时客户端会被提示（当前实现为强提示，不阻断使用）
 */

$opt = getopt('', ['channel::', 'dir:', 'url:', 'force::']);
$channel = $opt['channel'] ?? 'latest';
$dir     = rtrim($opt['dir'] ?? __DIR__ . '/pkgs', '/\\');
$baseUrl = rtrim($opt['url'] ?? '', '/');
$force   = isset($opt['force']) && $opt['force'] !== 'false';

if (!is_dir($dir)) {
    fwrite(STDERR, "目录不存在: $dir\n");
    exit(1);
}
if ($baseUrl === '') {
    fwrite(STDERR, "必须提供 --url（下载地址前缀）\n");
    exit(1);
}

$best = null;
$bestVer = null;
foreach (glob($dir . '/MonitorOSD_*.exe') as $f) {
    if (!preg_match('/MonitorOSD_([0-9]+(?:\.[0-9]+)*)\.exe$/i', basename($f), $m)) continue;
    $ver = $m[1];
    $parts = array_map('intval', explode('.', $ver));
    if ($bestVer === null || version_compare($ver, $bestVer, '>')) {
        $bestVer = $ver;
        $best = $f;
    }
}
if (!$best) {
    fwrite(STDERR, "在 $dir 下没找到 MonitorOSD_<版本>.exe\n");
    exit(1);
}

// ---- 三方校验：文件名版本 == exe 内嵌 FILEVERSION ----
// 背景：v1.5.1/v1.5.2 发布包曾经装的都是同一个 1.5.1 二进制（只改了文件名和说明文档），
// 客户端自认旧版 → 反复提示更新 → 永不收敛。这里在出清单前把它拦下来。
//
// 实现说明：不依赖 Windows API（服务器是 Linux/宝塔），纯 PHP 解析 PE。
// 思路：找到资源节 → 在节内扫描 VS_FIXEDFILEINFO 的签名 FEEF04BD →
//       该结构体偏移 +8 是 dwFileVersionMS、+12 是 dwFileVersionLS。
function read_pe_fileversion(string $path): ?string {
    $fh = @fopen($path, 'rb');
    if (!$fh) return null;
    $head = fread($fh, 0x40);
    if (strlen($head) < 0x40 || substr($head, 0, 2) !== 'MZ') { fclose($fh); return null; }

    $peOff = unpack('V', substr($head, 0x3C, 4))[1];
    fseek($fh, $peOff);
    $peHdr = fread($fh, 24);
    if (strlen($peHdr) < 24 || substr($peHdr, 0, 4) !== "PE\0\0") { fclose($fh); return null; }

    $numSec  = unpack('v', substr($peHdr, 6, 2))[1];
    $optSize = unpack('v', substr($peHdr, 20, 2))[1];

    // 可选头（PE32+ 224 字节 / PE32 96 字节起算数据目录）
    fseek($fh, $peOff + 24);
    $opt = fread($fh, $optSize);
    if (strlen($opt) < 2) { fclose($fh); return null; }
    $magic = unpack('v', substr($opt, 0, 2))[1];
    if ($magic === 0x20B)      $ddBase = 112;   // PE32+
    elseif ($magic === 0x10B)  $ddBase = 96;    // PE32
    else { fclose($fh); return null; }

    // 数据目录索引 2 = 资源表（IMAGE_DIRECTORY_ENTRY_RESOURCE）
    $resRva = unpack('V', substr($opt, $ddBase + 2 * 8, 4))[1];
    if ($resRva === 0) { fclose($fh); return null; }

    // 段表：RVA → 文件偏移
    fseek($fh, $peOff + 24 + $optSize);
    $secs = fread($fh, 40 * $numSec);
    $resFile = null; $resSize = 0;
    for ($i = 0; $i < $numSec; $i++) {
        $s = substr($secs, $i * 40, 40);
        if (strlen($s) < 40) break;
        $vsize = unpack('V', substr($s, 8, 4))[1];
        $vaddr = unpack('V', substr($s, 12, 4))[1];
        $rsize = unpack('V', substr($s, 16, 4))[1];
        $roff  = unpack('V', substr($s, 20, 4))[1];
        if ($resRva >= $vaddr && $resRva < $vaddr + max($vsize, 1)) {
            $resFile = $roff + ($resRva - $vaddr);
            // SizeOfRawData 为 0 的节（某些链接器输出）回退用 VirtualSize
            $resSize = ($rsize > 0) ? $rsize : $vsize;
            break;
        }
    }
    if ($resFile === null || $resSize <= 0) { fclose($fh); return null; }
    if ($resSize > 64 * 1024 * 1024) $resSize = 64 * 1024 * 1024;   // 防御：异常大不读

    fseek($fh, $resFile);
    $res = fread($fh, $resSize);
    fclose($fh);
    if ($res === false || $res === '') return null;

    // VS_FIXEDFILEINFO: dwSignature(FEEF04BD) + dwStrucVersion + dwFileVersionMS + dwFileVersionLS
    $pos = strpos($res, "\xBD\x04\xEF\xFE");
    if ($pos === false || $pos + 16 > strlen($res)) return null;
    $ms = unpack('V', substr($res, $pos + 8, 4))[1];
    $ls = unpack('V', substr($res, $pos + 12, 4))[1];
    if ($ms === 0 && $ls === 0) return null;      // 没填版本号
    return (($ms >> 16) & 0xFFFF) . '.' . ($ms & 0xFFFF) . '.' . (($ls >> 16) & 0xFFFF);
}

$exeVer = read_pe_fileversion($best);
if ($exeVer === null) {
    fwrite(STDERR, "[失败] 无法读取 " . basename($best) . " 的内嵌版本号，拒绝出清单\n");
    exit(1);
}
if (version_compare($exeVer, $bestVer, '!=')) {
    fwrite(STDERR, "[失败] 版本不一致：文件名={$bestVer}  二进制内嵌={$exeVer}\n");
    fwrite(STDERR, "       客户端会因内嵌版本偏旧而反复提示更新。请用正确版本重新编译后再发布。\n");
    exit(1);
}
echo "版本校验通过：{$exeVer}（文件名与二进制一致）\n";

$notes = [];
$notesFile = preg_replace('/\.exe$/i', '.notes.txt', $best);
if (is_file($notesFile)) {
    foreach (file($notesFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $l) {
        $notes[] = trim($l);
    }
}

$manifest = [
    'version' => $bestVer,
    'url'     => $baseUrl . '/' . basename($best),
    'sha256'  => hash_file('sha256', $best),
    'size'    => filesize($best),
    'force'   => $force,
    'notes'   => $notes,
    'pubtime' => date('Y-m-d H:i:s'),
];

$out = $dir . '/../' . $channel . '.json';
file_put_contents($out, json_encode($manifest, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT));

echo "已生成 $out\n";
echo "  版本 {$manifest['version']}\n";
echo "  sha256 {$manifest['sha256']}\n";
echo "  大小 {$manifest['size']} bytes\n";
