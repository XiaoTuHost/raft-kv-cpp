# documents

**存放项目文档**

## 分布式/Raft相关理论

### 什么是脑裂(split-brain)

集群出现分区问题，各个分区的节点各自独立地向外界提供服务，而忽视了分布式集群这个大的整体。

### 大多数原则(majority rule)

raft协议可用于解决单点故障和分区问题，raft协议采用了majority rule。majority rule可用于解决脑裂问题。

majority rule即少数节点服从对数节点，只有当一个操作获得超过半数（N/2 + 1）节点的认可后，才能被视为有效。

majority rule保证了在发生网络分区时，只有一个拥有多数节点的分区能够继续工作。

### 利用raft构建复制状态机

raft是一个库，他与上层应用程序绑定。

client查询raft节点，与leader节点通信

leader收到请求后记录到log

leader将接收到的log后同步给follower节点

follower节点收到log后返回ack给leader

leader收到超过半数的ack后提交日志，将日志内容应用到上层程序，告诉follower也可执行提交操作

### raft 使用流程

### raft日志

#### 用途

重传：同步消息可能会丢失，需记录后重传

顺序：同步操作，相同顺序出现在应用程序上

持久化：持久化log数据，支持失败重传，快速重启恢复

试探性操作：试探哪些操作需要提交

#### 格式

log中含有多个log entry。每个log entry被log index、leader term唯一标识。

log entry包含如下信息：command、leader term

### raft选举

在触发超时选举后成为candidate之后，对自己投票，向其他节点发送投票请求

获得超半数节点的票数后可成为leader

follower可拒绝投票：1. 自己任期更大 2. 同任期下自己日志更新

follower拒绝投票篇说明candidate存在当即丢失信息的情况，为保留最新信息，拒绝投票

#### 选举分裂问题

如果超时选举的超时时间相同，那么会多个follower节点进入candidate状态导致选举分裂

采用随机的超时选举时间，论文建议150ms-300ms之间

保证了多次操作后只诞生一个leader

### 日志分叉

不同节点的日志在同一索引位置（Index）存在冲突的日志条目，导致集群的日志出现不一致。这种现象通常发生在网络分区、节点故障或旧的 Leader 未及时退位等场景中。Raft 通过其强 Leader 特性和日志一致性规则自动检测并修复日志分叉，确保最终所有节点的日志达成一致。

### 日志追赶/覆写同步

next Index:所有raft节点维护的乐观的变量用于记录下一个log entry的log index

match Index:所有raft节点维护的悲观的变量用于记录从0开始与leader相比最长能匹配上的log index的位置+1



#### 流程

leader收到请求后追加到本地日志，发送append entries rpc到follower，包含了leader log的pre Index和pre term。

follower收到后开始比对pre index 位置的term是否一致

不一致则前向纠错，follower的next index-1，leader 的pre log index-1，重新发送rpc请求，重复此上操作直至匹配

### 持久化

考虑reboot后的场景

策略1：接收leader的log开始重新同步信息。代价高。

策略2：快速重启，从上一次持久化的状态恢复。后续可以通过log catch up的机制，赶上leader的log状态

人们偏向于策略2，快速重启。



持久化内容：

vote for

log

current term

**为什么需要持久化vote term：**防止节点重启后重复投票，导致同一任期内出现多个 Leader

**思考：什么时候持久化（优化点）**

### 服务恢复

策略1：日志重复

策略2：周期性快照

### 快照

raft用于日志压缩的机制

快照可以删除已应用到状态机的旧日志，允许节点直接从快照恢复状态机，无需重放全部历史日志。

快照是持久化的一部分，解决持久化容错问题

### 线性一致性

服务端的一系列操作对应客户端的一系列操作，且执行顺序与客户端请求的顺序一致。每次读操作都能获得最新的写操作。

## RPC理论

### 什么是RPC

rpc即远端过程调用，它使得调用一个在其它主机上的远程方法可以向调用本地方法一样。在使用中编写就和调用本地方法一样，但是其背后是通过rpc框架与远端服务器通信协同完成的。



## 跳表（SkipList）理论

**跳表是基于链表的数据结构，在链表上改进，通过引入冗余节点，增加每个节点的层级，使得查询操作无需逐个查询，可以“跳过”多个节点，优化了链表的查询效率，其查询时间复杂度可以达到O(logN)，而传统的链表查询时间复杂度为O(N)**

### 跳表基本结构

### 跳表查询

### 跳表插入

### 跳表删除

### 跳表遍历