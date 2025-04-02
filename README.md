# Traffic-Analysis-Base-DPDK
基于DPDK的加密流量采集分析系统

## 简介
该系统是一款面向大规模网络流量处理的高性能平台，专注于流量采集、用户画像、异常检测和业务分类等核心功能。依托高效的数据处理框架，系统实现了高速流量采集与解析，支持多维度特征提取和自动化分析，能够满足复杂场景下的实时处理需求。

系统以流量采集为核心，结合用户画像和异常检测模块，精准分析网络行为特征，及时识别异常活动，保障数据处理的可靠性和安全性。同时，通过业务分类模块，进一步挖掘数据价值，助力业务优化与智能管理。

凭借灵活的架构和前瞻性的技术设计，该系统突破了传统流量分析的性能瓶颈，提供从流量采集到深度分析的完整解决方案，适用于多种复杂网络环境和应用场景。

## 效果展示
![image](https://github.com/Battlingboy/Traffic-Analysis-Base-DPDK/blob/main/pre.GIF)

## 文件内容介绍
### Backend
系统后端
### Backend_Dpdk_Collect
后端Dpdk采集部分
### Frontend
系统前端
### Frontend_Packet_Detail
数据包详情部分前端

## 安装方法
- 首先将所有文件中“【】”标记处修改为需要的信息（包括自定义的用户名、密码、IP地址等）。
- 安装所需环境
### 系统后端部分安装方法
1. 将Backend文件夹传输到需部署服务器
2. mkdir -p /app/classify/dataset/pcaps/
3. mkdir -p /data/
4. ln -s /app/pcaps /data/pcaps
5. 将后端文件传入/app/目录
6. 配置mysql、neo4j，根据需要创建对应的数据库、表
7. systemctl enable mysql并启动neo4j
### Dpdk采集部分安装方法
1. 从官网下载并安装DPDK
2. 设置HugePage、绑定网卡驱动（dpdk-devbind.py --bind vfio-pci ens34）
3. 将Backend_Dpdk_Collect文件夹内容移动到需要的位置，进入文件夹根目录
4. meson build
5. cd build
6. ninja
7. build文件夹中得到的可执行文件就是采集程序
8. 将可执行文件移动到/app/classify/dpdk_anomaly_flow_detector
9. chmod +x /app/classify/dpdk_anomaly_flow_detector
### 系统前端安装方法
1. 在Frontend文件夹根目录执行npm run push
2. 配置nginx内容

```
cat << EOF > /etc/nginx/sites-available/default
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    root /web/;
    client_max_body_size 0;
    server_name _;
    location /nmas/ {
        proxy_pass http://127.0.0.1:8080/;
        proxy_connect_timeout 60s;
        proxy_read_timeout 365d;
        proxy_send_timeout 60s;
    }
    location /nmas_data/ {
        alias /app/pcaps/;
    }
    location / {
        index index.html index.htm;
    }
}
EOF
```
3. mkdir -p /web/并将编译后的内容移动到该目录
4. chown -R www-data /web/
5. chgrp -R www-data /web/
6. chmod -R 755 /web/
7. systemctl enable nginx
8. systemctl restart nginx
### 数据包详情部分安装方法
1. 在Frontend_Packet_Detail目录执行npm run push
2. 将编译后的内容移动到前端部分的packet_info文件夹

## 系统启动方法
```
pushd /app/

# 本文中修改后的大系统后端 依赖以下环境变量
export NEO4J_SERVER=http://127.0.0.1:7474/
export NEO4J_DATABASE=neo4j
export NEO4J_USER=neo4j
export NEO4J_PASSWORD=【】

export MYSQL_HOST=127.0.0.1
export MYSQL_PORT=【】
export MYSQL_DATABASE=【】
export MYSQL_USER=【】
export MYSQL_PASSWORD=【】

export REDIS_HOST=127.0.0.1
export REDIS_PORT=【】
export REDIS_PASSWORD=【】
export REDIS_DB=【】

export TSHARK_BIN=tshark
export SHARKD=sharkd

python3 manage.py runserver 0.0.0.0:8080
nohup python3 manage.py runserver 0.0.0.0:8080 &

popd
```

## 使用方法
### 首页
首页介绍了系统的主要功能，并提供了进入各个功能页的入口链接，可以直接点击前往。
### 流量采集
1. 页面顶部卡片框中可输入过滤规则
2. 点击开始采集即可启动采集程序，点击停止可以关闭采集程序并显示采集信息曲线
3. 点击特征提取可以使用双向流特征提取功能，直接进行特征提取，效率更高
4. 页面下方为数据包显示区域，可以查看各个采集节点的数据包，并且可以下载至本地使用
5. 点击数据包名称即可进行数据包在线解析
### 异常检测
1. 点击页面顶部的开始检测按钮可以开始进行异常检测
2. 页面主体部分显示检测到的异常ip及其异常类型
3. 点击查看可以看到异常IP的具体信息
### 用户画像
1. 进入页面后等待加载，完成后显示各个IP之间的关联
2. 点击显示地图可以看到各个IP的地理位置，观察不同地点之间的数据关联
### 业务分类
1. 在业务类别映射页面，可以自由增减业务类型
2. 在业务分类模型训练页面，可以执行模型训练和数据集生成操作
3. 在业务分类页面，根据需求选择各个选项后，点击开始分类，等待分类完成后将在页面下方显示分类结果
## FAQ
1. 如何解决采集无法停止的问题？
- 通常问题源于DPDK后端采集部署问题，检查后端文件夹中的nohup.txt（推荐使用tail -f nohup.txt），根据报错信息进行修复。

## Contact
For issues, please open an [issue](https://github.com/Battlingboy/Traffic-Analysis-Base-DPDK/issues).
