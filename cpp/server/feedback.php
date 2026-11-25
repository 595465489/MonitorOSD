<?php
/**
 * MonitorOSD 在线反馈接收端  —— 宝塔面板 + PHP 7.4+ 直接可用
 *
 * 部署位置：{更新根目录}/feedback.php
 *   例：站点根目录 /www/wwwroot/osd.example.com/api/feedback.php
 *
 * 客户端（C++ 程序）POST JSON，字段：
 *   type / version / text / extra / with_log / machine / user / os / monitors / log / hash
 *   hash = base64(HMAC-SHA256(json_before_hash, SALT))
 *
 * 安全要点：
 *   1) HMAC 校验挡掉无脑刷接口的脚本（盐值编译进 exe，不是强信任，但足够过滤噪声）
 *   2) 单 IP 限流（默认 20 条/小时）
 *   3) 落盘用 JSON Lines，一行一条，绝不执行用户内容
 *   4) 只返回「ok」或错误码，不回显任何输入
 */

// ============ 配置 ============
const FEEDBACK_SALT   = 'change-me-feedback-salt';   // 必须与 main.cpp 的 kFeedbackSalt 完全一致
const DATA_DIR        = __DIR__ . '/feedback';       // 数据目录（需 PHP 可写）
const RATE_PER_HOUR   = 20;                          // 每 IP 每小时上限
const MAX_BODY        = 262144;                      // 256KB 上限
const MAX_TEXT        = 4000;
// ==============================

header('Content-Type: application/json; charset=utf-8');
header('X-Content-Type-Options: nosniff');

function fail(int $code, string $msg): void {
    http_response_code($code);
    echo json_encode(['ok' => false, 'error' => $msg], JSON_UNESCAPED_UNICODE);
    exit;
}

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') fail(405, 'method');

$raw = file_get_contents('php://input', false, null, 0, MAX_BODY + 1);
if ($raw === false || strlen($raw) === 0) fail(400, 'empty');
if (strlen($raw) > MAX_BODY) fail(413, 'too large');

$data = json_decode($raw, true);
if (!is_array($data)) fail(400, 'bad json');

// ---- HMAC 校验：取 hash 字段前的原始字节参与计算 ----
$hash = isset($data['hash']) ? (string)$data['hash'] : '';
if ($hash === '') fail(400, 'no hash');
$pos = strrpos($raw, ',"hash"');
if ($pos === false) fail(400, 'bad layout');
$payload = substr($raw, 0, $pos) . '}';
$expect  = base64_encode(hash_hmac('sha256', $payload, FEEDBACK_SALT, true));
if (!hash_equals($expect, $hash)) fail(403, 'bad sign');

// ---- 限流 ----
if (!is_dir(DATA_DIR) && !@mkdir(DATA_DIR, 0755, true)) fail(500, 'mkdir');
$ip    = $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0';
$rateF = DATA_DIR . '/.rate_' . md5($ip);
$now   = time();
$hits  = [];
if (is_file($rateF)) {
    $hits = array_filter(
        json_decode((string)@file_get_contents($rateF), true) ?: [],
        fn($t) => is_int($t) && $t > $now - 3600
    );
}
if (count($hits) >= RATE_PER_HOUR) fail(429, 'rate limited');
$hits[] = $now;
@file_put_contents($rateF, json_encode(array_values($hits)), LOCK_EX);

// ---- 规范化落盘（只取白名单字段，长度截断）----
$clip = function ($v, int $max): string {
    $s = is_string($v) ? $v : '';
    $s = str_replace("\0", '', $s);
    return mb_substr($s, 0, $max, 'UTF-8');
};
$rec = [
    'time'     => date('Y-m-d H:i:s'),
    'ip'       => $ip,
    'type'     => $clip($data['type'] ?? '', 32),
    'version'  => $clip($data['version'] ?? '', 32),
    'text'     => $clip($data['text'] ?? '', MAX_TEXT),
    'extra'    => $clip($data['extra'] ?? '', MAX_TEXT),
    'machine'  => preg_replace('/[^a-f0-9]/', '', $clip($data['machine'] ?? '', 64)),
    'user'     => $clip($data['user'] ?? '', 64),
    'os'       => $clip($data['os'] ?? '', 32),
    'monitors' => $clip($data['monitors'] ?? '', 2000),
    'log'      => !empty($data['with_log']) ? $clip($data['log'] ?? '', 8000) : '',
    'ua'       => $clip($_SERVER['HTTP_USER_AGENT'] ?? '', 200),
];
$line = json_encode($rec, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) . "\n";

$month = DATA_DIR . '/' . date('Ym');
if (!is_dir($month) && !@mkdir($month, 0755, true)) fail(500, 'mkdir month');
$file = $month . '/feedback-' . date('d') . '.jsonl';
if (@file_put_contents($file, $line, FILE_APPEND | LOCK_EX) === false) fail(500, 'write');

echo json_encode(['ok' => true], JSON_UNESCAPED_UNICODE);
