# 第三方模块

MCP/ 是正式Git子模块，归属独立仓库 https://github.com/Qiao12-pixel/MCP.git。
AVPlayer通过.gitmodules记录地址、通过gitlink固定提交，不直接跟踪子模块内部源码。

```sh
git clone --recurse-submodules <AVPlayer仓库地址>
# 已有检出：
git submodule update --init --recursive
# 构建：
scripts/build_mcp.sh
```

子模块改动需要先在MCP内提交并推送，再在AVPlayer提交新的子模块指针。
不能只推送主仓库指针而不推送对应MCP提交，否则其他机器无法初始化该版本。
运行配置thirdParty/MCP/config由子模块.gitignore忽略，不属于主仓库或子模块提交。
来源和本地修订见MCP/UPSTREAM.md。
