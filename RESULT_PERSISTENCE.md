# 转写与摘要结果存储

## 保存内容

- 转写：完整带时间戳segments、展示结果以及供后续摘要/搜索使用的字幕文本。
- 摘要：完整摘要正文。
- MySQL `media_results` 为结果快照表，键是 `(session_id, media_id, task, variant)`；同键写入更新而不是重复插入。
- Redis使用 `avplayer:result:<session_id>:<media_id>:<task>:<variant>` 缓存完整JSON，TTL沿用AVPLAYER_REDIS_TTL（默认3600秒）。
- 转写额外同步既有 `transcripts` / `segments` 表与旧字幕缓存，兼容现有数据接口。
- `task_runs` 保留每次请求的执行历史，不能代替媒体结果快照。
- MCP仍在共享storage写入transcript.json/transcript.srt；文件导出能力不变。

Redis是缓存，并非本功能的持久化保证；MySQL负责结果跨进程重启保存。
媒体ID由上传生成，每次重新上传产生新ID，不按文件名自动复用；跨会话读取也不共享。

## Agent链路

1. 第一次转写/生成摘要仍由Agent编排，经MCP执行工具/调用模型。
2. 成功结果通过内部数据RPC提交统一C++后端；Python不连接Redis/MySQL。
3. 后端先更新MySQL快照，再刷新Redis；返回mysql_saved/redis_cached分别报告真实保存状态。
4. 同媒体后续请求优先Redis，未命中从MySQL读取并回填Redis。
5. 命中结果返回cached=true，跳过Whisper或摘要模型；完整segments仍返回前端显示字幕。

摘要variant为SHA256(provider + 换行 + 当前时间轴字幕文本)。切换文本模型provider或字幕内容改变后，会按新版本生成摘要，不复用旧版本。
当前不区分同provider内部模型版本/提示词版本，也没有手动强制重生成入口。
任务开始绑定的媒体ID保持稳定，执行期间其他上传不会将保存结果重定向到新媒体。

## 配置与更新

C++后端读取AVPLAYER_REDIS_URL、AVPLAYER_MYSQL_DSN、AVPLAYER_REDIS_TTL。
新表由后端启动时CREATE TABLE IF NOT EXISTS创建；现有数据表不删改。
更新后重建并重启avplayer_server和Agent：

```sh
cmake --build cmake-build-debug --target avplayer_server -j4
scripts/dev.sh
```

已有历史字幕文件不会在启动时批量导入。新任务成功生成后进入持久化链路；需要历史数据迁移时应单独执行导入。
MCP作为独立工具直接被其他调用方使用时，不自动更新AVPlayer数据库；此处集成由Agent协调。

## 查询

```sql
SELECT session_id, media_id, task, variant, updated_at
FROM avplayer.media_results ORDER BY updated_at DESC LIMIT 20;

SELECT payload
FROM avplayer.media_results
WHERE session_id = '实际会话ID'
  AND media_id = '实际媒体ID'
  AND task = 'summarize_video';

SELECT start_ms, end_ms, text
FROM avplayer.segments WHERE media_id = '实际媒体ID' ORDER BY idx;
```

## 故障边界

MySQL和Redis写入不是分布式事务。两者可独立成功或失败，返回persistence状态，不把缓存成功误报成持久化成功。
依赖不可用时保留MCP文件/已生成结果；MySQL未写入不能宣称跨重启可恢复。
当前数据连接沿用启动建立策略，数据库就绪后需重启后端恢复；没有新增自动重连保证。
会话隔离不是用户认证，内部数据RPC仅用于可信网络。

## 验证

```sh
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v
.venv/bin/python tests/result_persistence.py
```

集成测试使用51161/51162/51165/51166/51181和临时storage，要求真实Redis/MySQL、本机Whisper与Qwen模型。
覆盖完整结果写入、TTL、版本/会话隔离、删除缓存并重启后端后的MySQL回填、Agent重复请求复用，以及真实转写摘要自动保存。
测试会新增独立results-*会话的数据库记录，不删除现有数据。
