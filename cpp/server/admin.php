<?php
/**
 * MonitorOSD 反馈查看页 —— 宝塔建一个独立站点或放到 /api/feedback/admin.php，
 * 用宝塔「网站目录 → 访问限制」加 HTTP Basic 认证，不要裸奔。
 *
 * 功能：按月目录列出反馈，支持按类型/版本筛选、搜索、导出 CSV。
 */
const DATA_DIR = __DIR__ . '/../feedback';

$type    = $_GET['type'] ?? '';
$ver     = $_GET['ver'] ?? '';
$q       = trim($_GET['q'] ?? '');
$export  = isset($_GET['csv']);

$rows = [];
if (is_dir(DATA_DIR)) {
    foreach (glob(DATA_DIR . '/*/*.jsonl') as $f) {
        foreach (file($f, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $line) {
            $r = json_decode($line, true);
            if (!is_array($r)) continue;
            if ($type !== '' && ($r['type'] ?? '') !== $type) continue;
            if ($ver  !== '' && ($r['version'] ?? '') !== $ver) continue;
            if ($q !== '') {
                $hay = ($r['text'] ?? '') . ($r['extra'] ?? '') . ($r['monitors'] ?? '');
                if (mb_stripos($hay, $q, 0, 'UTF-8') === false) continue;
            }
            $rows[] = $r;
        }
    }
}
usort($rows, fn($a, $b) => strcmp($b['time'] ?? '', $a['time'] ?? ''));

if ($export) {
    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename=feedback-' . date('Ymd-His') . '.csv');
    $out = fopen('php://output', 'w');
    fwrite($out, "\xEF\xBB\xBF");   // BOM：Excel 中文不乱码
    fputcsv($out, ['时间', '类型', '版本', '内容', '补充', '用户', '机器码', '显示器', 'IP']);
    foreach ($rows as $r) {
        fputcsv($out, [
            $r['time'] ?? '', $r['type'] ?? '', $r['version'] ?? '', $r['text'] ?? '',
            $r['extra'] ?? '', $r['user'] ?? '', $r['machine'] ?? '',
            str_replace("\n", ' / ', $r['monitors'] ?? ''), $r['ip'] ?? '',
        ]);
    }
    fclose($out);
    exit;
}

$types = [];
$vers  = [];
foreach ($rows as $r) {
    $types[$r['type'] ?? ''] = true;
    $vers[$r['version'] ?? ''] = true;
}
?><!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>MonitorOSD 反馈（<?= count($rows) ?> 条）</title>
<style>
  :root { color-scheme: light; }
  body { font: 14px/1.6 "Microsoft YaHei UI", system-ui, sans-serif; margin: 0; padding: 24px;
         background: #f6f7f9; color: #1c1e21; }
  h1 { font-size: 18px; margin: 0 0 16px; }
  form { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 16px; }
  select, input[type=search] { padding: 6px 10px; border: 1px solid #d0d4da; border-radius: 6px; background: #fff; }
  button, .btn { padding: 6px 14px; border: 0; border-radius: 6px; background: #f04438; color: #fff;
                 cursor: pointer; text-decoration: none; font-size: 13px; }
  .card { background: #fff; border: 1px solid #e6e8eb; border-radius: 10px; padding: 14px 16px; margin-bottom: 10px; }
  .meta { color: #6b7280; font-size: 12px; margin-bottom: 6px; }
  .tag { display: inline-block; padding: 1px 8px; border-radius: 99px; background: #eef1f5; margin-right: 6px; }
  .text { white-space: pre-wrap; word-break: break-word; }
  details { margin-top: 8px; color: #4b5563; }
  pre { background: #0a0a0b; color: #d7dae0; padding: 10px; border-radius: 8px; overflow: auto; font-size: 12px; }
  .empty { color: #6b7280; padding: 40px; text-align: center; }
</style>
</head>
<body>
<h1>MonitorOSD 意见反馈 <span style="color:#6b7280;font-size:13px">共 <?= count($rows) ?> 条</span></h1>
<form method="get">
  <select name="type">
    <option value="">全部类型</option>
    <?php foreach (array_keys($types) as $t): if ($t === '') continue; ?>
      <option value="<?= htmlspecialchars($t) ?>" <?= $t === $type ? 'selected' : '' ?>><?= htmlspecialchars($t) ?></option>
    <?php endforeach; ?>
  </select>
  <select name="ver">
    <option value="">全部版本</option>
    <?php foreach (array_keys($vers) as $v): if ($v === '') continue; ?>
      <option value="<?= htmlspecialchars($v) ?>" <?= $v === $ver ? 'selected' : '' ?>>v<?= htmlspecialchars($v) ?></option>
    <?php endforeach; ?>
  </select>
  <input type="search" name="q" placeholder="搜索内容 / 显示器型号" value="<?= htmlspecialchars($q) ?>" size="28">
  <button type="submit">筛选</button>
  <a class="btn" style="background:#374151" href="?<?= http_build_query(['type'=>$type,'ver'=>$ver,'q'=>$q,'csv'=>1]) ?>">导出 CSV</a>
</form>

<?php if (!$rows): ?>
  <div class="empty">没有匹配的反馈</div>
<?php endif; ?>

<?php foreach ($rows as $r): ?>
  <div class="card">
    <div class="meta">
      <span class="tag"><?= htmlspecialchars($r['type'] ?? '') ?></span>
      <span class="tag">v<?= htmlspecialchars($r['version'] ?? '') ?></span>
      <?= htmlspecialchars($r['time'] ?? '') ?> ·
      <?= htmlspecialchars($r['user'] ?? '') ?> · 机器 <?= htmlspecialchars($r['machine'] ?? '') ?> ·
      <?= htmlspecialchars($r['ip'] ?? '') ?>
    </div>
    <div class="text"><?= htmlspecialchars($r['text'] ?? '') ?></div>
    <?php if (!empty($r['extra'])): ?>
      <details open><summary>补充信息</summary><div class="text"><?= htmlspecialchars($r['extra']) ?></div></details>
    <?php endif; ?>
    <?php if (!empty($r['monitors'])): ?>
      <details><summary>显示器信息</summary><div class="text"><?= htmlspecialchars($r['monitors']) ?></div></details>
    <?php endif; ?>
    <?php if (!empty($r['log'])): ?>
      <details><summary>诊断日志</summary><pre><?= htmlspecialchars($r['log']) ?></pre></details>
    <?php endif; ?>
  </div>
<?php endforeach; ?>
</body>
</html>
