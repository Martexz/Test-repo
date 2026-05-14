<center> <h2> 计算机网络实验2 - 设计可靠传输协议并编程实现 </h2> </center>
<center> <h4> 学号：2311807  姓名：问丕丕 </h4> </center>

# 1. 实验要求
利用数据报套接字在用户空间实现面向连接的可靠数据传输，功能包括：
1. 连接管理：包括建立连接、关闭连接和异常处理。
2. 差错检测：使用校验和进行差错检测。
3. 确认重传：支持流水线方式，采用选择确认。
4. 流量控制：发送窗口和接收窗口使用相同的固定大小窗口。
5. 拥塞控制：实现RENO算法。

实验要求：

1. 实现单向数据传输，控制信息需要实现双向交互。
2. 给出详细的协议设计说明。
3. 给出详细的实现方法说明。
4. 利用C或C++语言，使用基本的Socket函数进行程序编写，不允许使用CSocket等封装后的类。
5. 在规定的测试环境中，完成给定测试文件的传输，显示传输时间和平均吞吐率，并观察不同发送窗口和接收窗口大小对传输性能的影响，以及不同丢包率对传输性能的影响。
6. 编写的程序应该结构清晰，具有较好的可读性。
7. 提交程序源码、可执行文件和实验报告。

# 2. 实验过程
## 2.1 整体协议设计

本实验设计并实现了一个基于 UDP 的可靠数据传输协议，支持连接管理、差错检测、确认重传、流量控制以及 RENO 拥塞控制算法。

### 2.1.1 报文格式设计
协议定义了统一的报文结构 `Packet`，包含头部 `PacketHeader` 和数据负载 `data`。

*   **头部结构 (PacketHeader)**：
    *   `seq`：序列号，用于标识报文顺序。
    *   `ack`：确认号，用于确认已收到的报文。
    *   `window`：接收窗口大小，用于流量控制。
    *   `checksum`：校验和，用于差错检测。
    *   `data_len`：数据载荷的实际长度。
    *   `flags`：控制标志位。
        *   `SYN` (0x01)：同步标志，用于建立连接。
        *   `ACK` (0x02)：确认标志，表示确认号有效。
        *   `FIN` (0x04)：结束标志，用于释放连接。
    *   `sack_count`：SACK块数量，指示后续 `sack_blocks` 中有效块的个数。
    *   `sack_blocks`：SACK块数组，用于携带接收方已收到的乱序报文段信息（起始序号和结束序号）。
*   **数据负载**：最大长度定义为 `MSS` (1024字节)。

### 2.1.2 连接管理
*   **连接建立**：采用握手机制。
    1.  发送方发送 `SYN` 报文（`seq=0`）。
    2.  接收方收到 `SYN` 后，回复 `SYN+ACK` 报文，确认连接建立。
    3.  发送方收到 `SYN+ACK` 后，开始发送数据。
*   **连接释放**：采用挥手机制。
    1.  发送方数据发送完毕后，发送 `FIN` 报文。
    2.  接收方收到 `FIN` 后，回复 `FIN+ACK` 报文，并关闭接收端。
    3.  发送方收到 `FIN+ACK` 后，关闭发送端。

### 2.1.3 可靠传输机制
实现 `calculate_checksum` 函数，采用 16 位反码求和算法计算校验和。发送方计算并填充校验和，接收方重新计算并验证。若校验失败，直接丢弃报文。
采用**选择确认 (Selective ACK)** 策略。接收方在 ACK 报文中除了包含累积确认号（`ack` 字段，表示期望收到的下一个有序报文序号）外，还通过 `sack_blocks` 字段携带已收到的乱序报文段信息。发送方根据 SACK 信息标记已成功送达的报文，避免不必要的重传。
发送方为最早未确认的报文（`base`）维护定时器。若在 RTO (0.5秒) 内未收到确认，则触发超时重传。

### 2.1.4 流量控制
*   采用滑动窗口机制。发送方的发送窗口大小受限于接收方通告的窗口大小 (`window_size`)。
*   接收方维护一个接收缓冲区 (`buffer`)，用于暂存乱序到达的报文，确保按序交付给上层应用。

### 2.1.5 拥塞控制 (RENO算法)
发送方维护拥塞窗口 `cwnd` 和慢启动阈值 `ssthresh`，实现 RENO 算法的四个阶段：

*   **慢启动**：连接初始化或超时后，`cwnd` 设为 1 MSS。每收到一个新 ACK，`cwnd` 增加 1 MSS，呈指数增长。
*   **拥塞避免**：当 `cwnd >= ssthresh` 时，每收到一个新 ACK，`cwnd` 增加 `MSS * MSS / cwnd`，呈线性增长。
*   **快重传**：当发送方收到 3 个重复 ACK（即针对同一乱序报文的确认，导致 `dup_acks` 计数达到 3）时，认为报文丢失，立即重传最早未确认的报文 (`base`)，而不必等待超时。
*   **快恢复**：快重传后，设置 `ssthresh = max(2*MSS, cwnd/2)`，`cwnd = ssthresh + 3*MSS`，进入拥塞避免阶段（或快速恢复阶段，视具体实现细节而定，本实现简化为调整后继续发送）。

## 2.3 具体实现

### 2.3.1 发送端 (Sender) 实现
发送端由 `Sender` 类封装，核心逻辑如下：

*   **初始化**：创建 UDP 套接字，绑定本地端口，加载传输文件并切分为 `Packet` 列表。
*   **握手与挥手**：`handshake()` 和 `teardown()` 函数分别实现连接建立和断开，使用阻塞式 `select` 等待对方响应，支持超时重试。

    ```cpp
    void Sender::handshake() {
        // Send SYN
        Packet syn_pkt;
        // ... (初始化 SYN 包)
        
        while (true) {
            sendto(sockfd, (char*)&syn_pkt, sizeof(PacketHeader), 0, (sockaddr*)&server_addr, sizeof(server_addr));
            
            // Wait for SYN-ACK
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);
            
            timeval timeout;
            timeout.tv_sec = 1; // 1 second timeout
            
            int activity = select(0, &readfds, NULL, NULL, &timeout);
            
            if (activity > 0) {
                // ... (接收并校验 SYN-ACK)
                if (len > 0 && verify_checksum(ack_pkt) && (ack_pkt.header.flags & (FLAG_SYN | FLAG_ACK))) {
                    break;
                }
            }
        }
    }
    ```

*   **数据发送循环 (`send_data`)**：
    *  计算有效窗口大小 `effective_window = min(window_size, cwnd/MSS)`。
    *  在窗口范围内，检查 `next_seq_num` 是否小于 `base + effective_window`，若是且未被确认，则调用 `send_packet_by_index` 发送报文，并记录发送时间。

        ```cpp
        int effective_window = std::min((int)window_size, (int)(cwnd / MSS));
        if (effective_window < 1) effective_window = 1;
        
        while (next_seq_num < packets.size() && next_seq_num < base + effective_window) {
            if (!acked[next_seq_num]) { 
                 send_packet_by_index(next_seq_num);
            }
            next_seq_num++;
        }
        ```

    *   **ACK 接收与处理**：
        *   使用 `select` 非阻塞监听 Socket。
        *   收到 ACK 后，首先处理累积确认号 `ack`，将所有小于 `ack` 的报文标记为已确认。
        *   **SACK 处理**：遍历 `sack_blocks`，将 SACK 块指示的范围内的报文标记为已确认。
        *   **新 ACK 处理**：若确认了 `base` 报文，滑动 `base` 窗口至下一个未确认位置。根据当前状态（慢启动或拥塞避免）更新 `cwnd`，重置 `dup_acks`。
        *   **重复 ACK 处理**：若确认了 `base` 之后的报文（乱序 ACK），增加 `dup_acks`。若 `dup_acks == 3`，触发**快重传**：重发 `base` 报文，更新 `ssthresh` 和 `cwnd`。

        ```cpp
        // 处理累计确认号
        for (uint32_t seq = 1; seq < ack_seq && seq <= packets.size(); seq++) {
            int idx = seq - 1;
            if (idx >= 0 && idx < packets.size()) acked[idx] = true;
        }
        
        // 处理SACK信息
        for (int i = 0; i < ack_pkt.header.sack_count; i++) {
            uint32_t start = ack_pkt.header.sack_blocks[i * 2];
            uint32_t end = ack_pkt.header.sack_blocks[i * 2 + 1];
            for (uint32_t seq = start; seq < end; seq++) {
                int idx = seq - 1;
                if (idx >= 0 && !acked[idx]) acked[idx] = true;
            }
        }

        // 滑动窗口与拥塞控制
        if (base > old_base) {
            // New ACK
            if (cwnd < ssthresh) cwnd += MSS * (base - old_base); // Slow Start
            else cwnd += MSS * MSS / cwnd * (base - old_base);    // Congestion Avoidance
            dup_acks = 0;
        } else {
            // Duplicate ACK
            dup_acks++;
            if (dup_acks == 3) {
                send_packet_by_index(base); // Fast Retransmit
                ssthresh = std::max((double)MSS * 2, cwnd / 2);
                cwnd = ssthresh + 3 * MSS;
            }
        }
        ```

    *   **超时处理**：检查 `base` 报文的发送时间戳。若 `current_time - send_time > 0.5s`，触发超时：重发 `base` 报文，设置 `ssthresh = max(2*MSS, cwnd/2)`，重置 `cwnd = MSS`（进入慢启动）。

        ```cpp
        if (base < packets.size() && !acked[base]) {
            if (send_times[base] > 0 && (current_time - send_times[base] > 0.5)) {
                send_packet_by_index(base);
                
                // RENO Timeout
                ssthresh = std::max((double)MSS * 2, cwnd / 2);
                cwnd = MSS;
                dup_acks = 0;
                send_times[base] = current_time; 
            }
        }
        ```

### 2.3.2 接收端 (Receiver) 实现
接收端由 `Receiver` 类封装，核心逻辑如下：

*   **初始化**：创建 UDP 套接字并绑定端口。
*   **握手响应**：`handle_handshake()` 等待 `SYN` 报文，回复 `SYN+ACK`，并初始化 `expected_seq`。
*   **数据接收循环 (`receive_data`)**：
    *   **接收与校验**：循环调用 `recvfrom` 接收报文，计算校验和。若校验失败则丢弃。
    *   **ACK 回复**：对每个通过校验的数据报文，立即回复 ACK，确认号为该报文的 `seq`。
    *   **按序交付**：
        *   维护 `expected_seq` 变量，记录期望收到的下一个序列号。
        *   若 `pkt.seq == expected_seq`：将数据写入文件，`expected_seq` 加 1。随后检查 `buffer`（`std::map`），若存在后续连续的报文，依次写入文件并更新 `expected_seq`，从 buffer 中移除。
        *   若 `pkt.seq > expected_seq`：说明发生乱序，将报文存入 `buffer` 中暂存，等待缺少的报文到达。

        ```cpp
        if (pkt.header.seq == expected_seq) {
            // In order
            outfile.write(pkt.data, pkt.header.data_len);
            expected_seq++;

            // Check buffer for subsequent packets
            while (buffer.count(expected_seq)) {
                Packet& buffered_pkt = buffer[expected_seq];
                outfile.write(buffered_pkt.data, buffered_pkt.header.data_len);
                buffer.erase(expected_seq);
                expected_seq++;
            }
        } else if (pkt.header.seq > expected_seq) {
            // Out of order, buffer it
            if (buffer.size() < window_size) {
                buffer[pkt.header.seq] = pkt;
            }
        }
        
        // 构建并发送带有 SACK 的 ACK
        Packet ack_pkt;
        ack_pkt.header.ack = expected_seq;
        ack_pkt.header.sack_count = 0;
        
        // 遍历 buffer 生成 SACK 块
        if (!buffer.empty()) {
            auto it = buffer.begin();
            int sack_idx = 0;
            while (it != buffer.end() && sack_idx < MAX_SACK_BLOCKS) {
                uint32_t start = it->first;
                uint32_t end = start;
                // 查找连续区间
                auto next_it = it; ++next_it;
                while (next_it != buffer.end() && next_it->first == end + 1) {
                    end = next_it->first; ++next_it;
                }
                ack_pkt.header.sack_blocks[sack_idx * 2] = start;
                ack_pkt.header.sack_blocks[sack_idx * 2 + 1] = end + 1;
                ack_pkt.header.sack_count++;
                sack_idx++;
                it = next_it;
            }
        }
        send_packet(ack_pkt);
        ```

    *   **连接结束**：收到 `FIN` 报文后，回复 `FIN+ACK` 并退出循环，完成文件接收。

### 2.3.3 辅助模块 (Utils)
*   `calculate_checksum`：实现了基于 16 位反码求和的校验和算法。

    ```cpp
    inline uint16_t calculate_checksum(const PacketHeader& header, const char* data) {
        uint32_t sum = 0;
        const uint16_t* ptr = (const uint16_t*)&header;
        // ... (Header calculation)
        
        // Data
        ptr = (const uint16_t*)data;
        int len = header.data_len;
        for (int i = 0; i < len / 2; ++i) {
            sum += ptr[i];
            if (sum & 0xFFFF0000) {
                sum &= 0xFFFF;
                sum++;
            }
        }
        // ... (Handle odd length)
        
        return ~(sum & 0xFFFF);
    }
    ```

*   `get_timestamp`：使用 `std::chrono::steady_clock` 提供高精度的秒级时间戳，用于 RTT 估算和超时判断。
    ```cpp
    inline double get_timestamp() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }
    ```

# 3. 实验结果检测
项目使用 CLion 编写，使用 CMake 构建为 Lab2.exe 后，将其和要发送的文件放在一起，然后配置提供的路由器程序：

由于是回环传输，所以路由器和服务器 IP 均为 127.0.0.1，路由器端口设置为 8888，服务器端口设置为 9999，如图
![alt text](pic/report/image.png){width=50%}

## 3.1 丢包率设为 0%
在未传输前：
![alt text](pic/report/image-1.png){width=50%}
使用以下命令传输三个文件(窗口为10)
```bash
# 接收端
.\Lab2.exe -r 9999 <FileName> 10
# 发送端
.\Lab2.exe -s 127.0.0.1 8888 1234 <FileName> 10
```
以 1.jpg 为例
![alt text](pic/report/image-2.png){width=50%}
![alt text](pic/report/image-3.png){width=50%}

## 3.2 接收端与发送端窗口不同对速率的影响
<div align="center">

<table>
  <tr>
    <td align="center">窗口为1</td>
    <td align="center">窗口为10</td>
    <td align="center">窗口为50</td>
  </tr>
  <tr>
    <td><img src="pic/report/image-9.png" width="300"/></td>
    <td><img src="pic/report/image-8.png" width="300"/></td>
    <td><img src="pic/report/image-10.png" width="300"/></td>
  </tr>
</table>

</div>
可以看出，窗口从1增大到10时，传输时间略有缩短，吞吐率提升；但继续增大到50时，提升并不明显，甚至略有波动。这说明在本地回环环境下，窗口大小对速率的影响有限，达到一定阈值后，进一步增大窗口对性能提升作用不大，主要受限于操作系统和协议实现的处理能力。

## 3.3 不同丢包率对速率的影响
双方窗口均为300，传输 3.jpg：
<div align="center">

<table>
  <tr>
    <td align="center">未丢包</td>
    <td align="center">丢包率1%</td>
    <td align="center">丢包率10%</td>
  </tr>
  <tr>
    <td><img src="pic/report/image-5.png" width="300"/></td>
    <td><img src="pic/report/image-6.png" width="300"/></td>
    <td><img src="pic/report/image-7.png" width="300"/></td>
  </tr>
</table>

</div>

可见丢包率越高，传输的速率越慢，消耗的时间越长。