# 第三方源码模块

MCP/作为普通源码目录纳入AVPlayer，由主仓库直接跟踪，不是Git子模块，不需要单独拉取或推送。
源码保留自己的构建入口，作为独立进程运行；在项目根目录执行scripts/build_mcp.sh构建到build/mcp。
来源和本地修订见MCP/UPSTREAM.md。原外部MCP仓库不受此次变更影响。
config目录中的本地配置由MCP/.gitignore忽略，构建产物、运行数据和凭据不提交。
