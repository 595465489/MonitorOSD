# 决策记录 · GitHub 附件命名

## GitHub 附件不能用中文文件名（2026-11-25）

- 背景：v1.6.6 发布安装包到 GitHub Releases，本地包名为「金橙电竞-显示器控制_v1.6.6.zip」。
- 选项：
  1. 上传后用网页编辑页改回中文名；
  2. 用 GitHub API PATCH 资产名改回中文；
  3. 接受英文名。
- 结论：采用选项 3，附件定名 `MonitorOSD_v1.6.6.zip`。
- 原因：实测 GitHub 服务器在保存资产时强制剥离文件名中的非 ASCII 字符——网页改名提交 4 轮均被改回 `-._v1.6.6.zip`；API PATCH 中文名同样返回 `-._v1.6.6.zip`（API PATCH 英文名则生效）。该清洗发生在服务端，客户端无法绕过。Release 标题、说明里的中文不受影响。
- 后续约定：发布包 zip 一律用英文/ASCII 文件名（如 `MonitorOSD_v版本号.zip`），中文品牌信息写在 Release 标题与说明中。
