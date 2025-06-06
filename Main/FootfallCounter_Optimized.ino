#include <ESP8266WiFi.h>
#include <Wire.h> 
#include "rgb_lcd.h"
#include <Ticker.h>
#include <ESP8266WebServer.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

// Wi-Fi 配置
const char* ssid = "WI-FI名称";
const char* password = "WI-FI密码";

// 定义传感器ID
const char* sensorId = "footfall_sensor_001";

// MQTT 服务器配置 (Adafruit IO)
#define AIO_SERVER "io.adafruit.com"
#define AIO_SERVERPORT 1883
#define AIO_USERNAME "ADA_IO_USER"  // 替换为您的Adafruit IO用户名
#define AIO_KEY "ADA_IO_KEY"  // 替换为您的Adafruit IO密钥

// MQTT 主题定义
#define FOOTFALL_FEED "yihequanming/feeds/Headcount"
#define AVERAGE_FEED "yihequanming/feeds/Average number of people per minute"
#define CURRENT_INFO_FEED "yihequanming/feeds/Current information"

// 创建ESP8266WebServer对象
ESP8266WebServer server(80);

// 引脚定义
const int LED_PIN = 0;    
const int BUTTON_PIN = 15; 
rgb_lcd lcd; 

// Ticker 对象
Ticker buttonPoller;
Ticker lcdUpdater;
Ticker minuteFootfallCalculator;

// 创建WiFi客户端和MQTT客户端
WiFiClient mqttClient;
Adafruit_MQTT_Client mqtt(&mqttClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// 创建MQTT发布对象
Adafruit_MQTT_Publish footfallPublish = Adafruit_MQTT_Publish(&mqtt, FOOTFALL_FEED);
Adafruit_MQTT_Publish averagePublish = Adafruit_MQTT_Publish(&mqtt, AVERAGE_FEED);
Adafruit_MQTT_Publish currentInfoPublish = Adafruit_MQTT_Publish(&mqtt, CURRENT_INFO_FEED);

// 全局变量
int footfallCount = 0;             
volatile int currentSecondFootfall = 0; 
volatile int lastMinuteFootfall[60];    
volatile int minuteFootfallIndex = 0;   
float averageFootfallPerMinute = 0.0;    
unsigned long lastMqttPublishTime = 0;
const long mqttPublishInterval = 5000;
int lastButtonState = HIGH;  // 添加这行来声明 lastButtonState 变量

// 添加用于跟踪Current information变化的变量
String lastPublishedCurrentInfo = "";
bool currentInfoPublished = false; 

// 函数声明
void connectToWiFi();
void pollButton();
void updateFootfallData();
void updateLcdDisplay();
void handleRoot();
void handleGetData();
void handleResetCounter();
void MQTT_connect();
void publishFootfallData();

// 优化后的HTML内容，存储在PROGMEM中
const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN" id="html-element">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>智能客流量监测系统</title>
    <!-- 引入Google字体 -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;600;700&family=Open+Sans:wght@400;600&family=Roboto+Mono:wght@400;600&display=swap" rel="stylesheet">
    <!-- 引入Font Awesome图标 -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <!-- 引入Chart.js -->
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root {
            --primary-color: #3498db;
            --secondary-color: #2ecc71;
            --warning-color: #e74c3c;
            --background-color: #f5f7fa;
            --card-background: #ffffff;
            --text-color: #2c3e50;
            --border-radius: 12px;
            --box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
            --transition-speed: 0.3s;
            --status-indicator-size: 8px;
        }

        /* 暗色模式变量 */
        :root.dark {
            --primary-color: #4285f4;
            --secondary-color: #0f9d58;
            --warning-color: #ea4335;
            --background-color: #121212;
            --card-background: #1e1e1e;
            --text-color: #e0e0e0;
            --box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Open Sans', sans-serif;
            background-color: var(--background-color);
            color: var(--text-color);
            line-height: 1.6;
            padding: 20px;
            max-width: 1200px;
            margin: 0 auto;
            transition: background-color var(--transition-speed), color var(--transition-speed);
        }

        header {
            display: flex;
            flex-direction: column;
            align-items: center;
            margin-bottom: 30px;
        }

        .header-controls {
            display: flex;
            gap: 15px;
            margin-top: 10px;
            align-items: center;
        }

        .current-date {
            font-size: 0.9rem;
            color: var(--text-color);
            opacity: 0.8;
            font-weight: 500;
            padding: 8px 12px;
            background: var(--card-background);
            border-radius: 8px;
            border: 1px solid #ecf0f1;
            min-width: 120px;
            text-align: center;
        }

        h1, h2, h3 {
            font-family: 'Montserrat', sans-serif;
            color: var(--text-color);
        }

        h1 {
            font-size: 2.2rem;
            margin-bottom: 10px;
            color: var(--primary-color);
        }

        .subtitle {
            font-size: 1rem;
            color: #7f8c8d;
            margin-bottom: 20px;
        }

        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }

        .card {
            background-color: var(--card-background);
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            padding: 20px;
            transition: transform var(--transition-speed), box-shadow var(--transition-speed);
        }

        .card:hover {
            transform: translateY(-5px);
            box-shadow: 0 6px 12px rgba(0, 0, 0, 0.15);
        }

        .card-title {
            font-size: 1rem;
            color: #7f8c8d;
            margin-bottom: 10px;
            display: flex;
            align-items: center;
        }

        .card-title i {
            margin-right: 8px;
            color: var(--primary-color);
        }

        .card-value {
            font-family: 'Roboto Mono', monospace;
            font-size: 2.5rem;
            font-weight: 600;
            margin-bottom: 10px;
            color: var(--text-color);
        }

        .trend {
            display: flex;
            align-items: center;
            font-size: 0.9rem;
        }

        .trend.up {
            color: var(--secondary-color);
        }

        .trend.down {
            color: var(--warning-color);
        }

        .trend i {
            margin-right: 5px;
        }

        .chart-container {
            background-color: var(--card-background);
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            padding: 20px;
            margin-bottom: 30px;
            height: 300px;
        }

        .info-section {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }

        .info-card {
            background-color: var(--card-background);
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            padding: 20px;
        }

        .info-item {
            display: flex;
            justify-content: space-between;
            margin-bottom: 10px;
            padding-bottom: 10px;
            border-bottom: 1px solid #ecf0f1;
        }

        .info-item:last-child {
            border-bottom: none;
            margin-bottom: 0;
            padding-bottom: 0;
        }

        .info-label {
            color: #7f8c8d;
        }

        .info-value {
            font-weight: 600;
        }

        .status-indicator {
            display: inline-block;
            width: var(--status-indicator-size);
            height: var(--status-indicator-size);
            border-radius: 50%;
            margin-right: 5px;
        }

        .controls {
            display: flex;
            flex-wrap: wrap;
            gap: 15px;
            margin-bottom: 30px;
            justify-content: center;
        }

        .btn {
            background-color: var(--primary-color);
            color: white;
            border: none;
            border-radius: 30px;
            padding: 12px 25px;
            font-family: 'Montserrat', sans-serif;
            font-weight: 600;
            cursor: pointer;
            transition: background-color var(--transition-speed), transform var(--transition-speed);
            display: flex;
            align-items: center;
            justify-content: center;
        }

        .btn i {
            margin-right: 8px;
        }

        .btn:hover {
            background-color: #2980b9;
            transform: translateY(-2px);
        }

        .btn.danger {
            background-color: var(--warning-color);
        }

        .btn.danger:hover {
            background-color: #c0392b;
        }

        .btn.secondary {
            background-color: #95a5a6;
        }

        .btn.secondary:hover {
            background-color: #7f8c8d;
        }

        .btn.small {
            padding: 8px 15px;
            font-size: 0.9rem;
        }

        .loading {
            position: fixed;
            top: 20px;
            right: 20px;
            background-color: rgba(0, 0, 0, 0.7);
            color: white;
            padding: 8px 15px;
            border-radius: 20px;
            font-size: 0.8rem;
            display: none;
            align-items: center;
        }

        .loading.active {
            display: flex;
        }

        .loading i {
            margin-right: 5px;
            animation: spin 1s linear infinite;
        }

        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        .pulse {
            animation: pulse 0.5s;
        }

        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.05); }
            100% { transform: scale(1); }
        }

        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.5);
            align-items: center;
            justify-content: center;
            z-index: 1000;
        }

        .modal-content {
            background-color: var(--card-background);
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            padding: 30px;
            max-width: 400px;
            width: 90%;
            text-align: center;
        }

        .modal-title {
            margin-bottom: 20px;
            color: var(--warning-color);
        }

        .modal-buttons {
            display: flex;
            justify-content: center;
            gap: 15px;
            margin-top: 20px;
        }

        footer {
            text-align: center;
            margin-top: 30px;
            color: #7f8c8d;
            font-size: 0.9rem;
        }

        /* 响应式调整 */
        @media (max-width: 768px) {
            .dashboard {
                grid-template-columns: 1fr;
            }
            
            .chart-container {
                height: 250px;
            }
            
            h1 {
                font-size: 1.8rem;
            }
            
            .card-value {
                font-size: 2rem;
            }
            
            .header-controls {
                flex-wrap: wrap;
                justify-content: center;
            }
        }

        /* 数据更新动画 */
        @keyframes highlight {
            0% { background-color: rgba(52, 152, 219, 0.2); }
            100% { background-color: transparent; }
        }

        .highlight {
            animation: highlight 1.5s ease-out;
        }
    </style>
</head>
<body>
    <header>
        <h1 id="page-title">智能客流量监测系统</h1>
        <p class="subtitle" id="page-subtitle">实时监测与数据分析</p>
        <div class="header-controls">
            <div class="current-date" id="current-date">--</div>
            <button class="btn small" id="dashboard-btn">
                <i class="fas fa-external-link-alt"></i> <span id="dashboard-text">仪表板</span>
            </button>
            <button class="btn small" id="lang-btn">
                <i class="fas fa-language"></i> <span id="lang-text">中文</span>
            </button>
            <button class="btn small" id="theme-btn">
                <i class="fas fa-moon"></i> <span id="theme-text">暗色模式</span>
            </button>
        </div>
    </header>

    <div class="dashboard">
        <div class="card" id="total-card">
            <div class="card-title" id="total-card-title">
                <i class="fas fa-users"></i><span>总客流量</span>
            </div>
            <div class="card-value" id="total-value">--</div>
            <div class="trend" id="total-trend">
                <i class="fas fa-arrow-up"></i>
                <span>加载中...</span>
            </div>
        </div>
        <div class="card" id="average-card">
            <div class="card-title" id="average-card-title">
                <i class="fas fa-chart-line"></i><span>每分钟平均客流量</span>
            </div>
            <div class="card-value" id="average-value">--</div>
            <div class="trend" id="average-trend">
                <i class="fas fa-arrow-up"></i>
                <span>加载中...</span>
            </div>
        </div>
    </div>

    <div class="chart-container">
        <canvas id="footfall-chart"></canvas>
    </div>

    <div class="info-section">
        <div class="info-card">
            <h3 id="sensor-info-title">传感器信息</h3>
            <div class="info-item">
                <div class="info-label" id="sensor-id-label">传感器ID</div>
                <div class="info-value" id="sensor-id">--</div>
            </div>
            <div class="info-item">
                <div class="info-label" id="device-ip-label">设备IP地址</div>
                <div class="info-value" id="device-ip">--</div>
            </div>
            <div class="info-item">
                <div class="info-label" id="uptime-label">运行时间</div>
                <div class="info-value" id="uptime">--</div>
            </div>
        </div>
        <div class="info-card">
            <h3 id="system-status-title">系统状态</h3>
            <div class="info-item">
                <div class="info-label" id="connection-status-label">连接状态</div>
                <div class="info-value" id="connection-status">
                    <span class="status-indicator">●</span> <span>连接中...</span>
                </div>
            </div>
            <div class="info-item">
                <div class="info-label" id="last-update-label">最后更新</div>
                <div class="info-value" id="last-update">--</div>
            </div>
            <div class="info-item">
                <div class="info-label" id="refresh-rate-label">数据刷新频率</div>
                <div class="info-value">2秒/次</div>
            </div>
        </div>
    </div>

    <div class="controls">
        <button class="btn danger" id="reset-btn">
            <i class="fas fa-redo-alt"></i><span>重置计数器</span>
        </button>
        <button class="btn secondary" id="export-btn">
            <i class="fas fa-download"></i><span>导出数据</span>
        </button>
    </div>

    <div class="loading" id="loading-indicator">
        <i class="fas fa-spinner"></i>
        <span>更新数据中...</span>
    </div>

    <div class="modal" id="reset-modal">
        <div class="modal-content">
            <h3 class="modal-title" id="reset-modal-title">确认重置</h3>
            <p id="reset-modal-message">您确定要重置客流量计数器吗？此操作不可撤销。</p>
            <div class="modal-buttons">
                <button class="btn secondary" id="cancel-reset">
                    <i class="fas fa-times"></i><span>取消</span>
                </button>
                <button class="btn danger" id="confirm-reset">
                    <i class="fas fa-check"></i><span>确认重置</span>
                </button>
            </div>
        </div>
    </div>

    <footer>
        <p id="footer-text">© 2025 智能客流量监测系统 | 实时数据监控与分析</p>
    </footer>

    <script>
        // 多语言翻译数据
        const translations = {
            zh: {
                pageTitle: "智能客流量监测系统",
                pageSubtitle: "实时监测与数据分析",
                totalCardTitle: "总客流量",
                averageCardTitle: "每分钟平均客流量",
                sensorInfoTitle: "传感器信息",
                sensorIdLabel: "传感器ID",
                deviceIpLabel: "设备IP地址",
                uptimeLabel: "运行时间",
                systemStatusTitle: "系统状态",
                connectionStatusLabel: "连接状态",
                lastUpdateLabel: "最后更新",
                refreshRateLabel: "数据刷新频率",
                xAxisLabel: "时间",
                yAxisLabel: "每分钟平均客流量",
                resetBtn: "重置计数器",
                exportBtn: "导出数据",
                dashboardBtn: "仪表板",
                resetModalTitle: "确认重置",
                resetModalMessage: "您确定要重置客流量计数器吗？此操作不可撤销。",
                cancelReset: "取消",
                confirmReset: "确认重置",
                footerText: "© 2025 智能客流量监测系统 | 实时数据监控与分析",
                langText: "中文",
                themeText: "暗色模式"
            },
            en: {
                pageTitle: "Intelligent Footfall Monitoring System",
                pageSubtitle: "Real-time Monitoring and Data Analysis",
                totalCardTitle: "Total Footfall",
                averageCardTitle: "Average Footfall per Minute",
                sensorInfoTitle: "Sensor Information",
                sensorIdLabel: "Sensor ID",
                deviceIpLabel: "Device IP Address",
                uptimeLabel: "Uptime",
                systemStatusTitle: "System Status",
                connectionStatusLabel: "Connection Status",
                lastUpdateLabel: "Last Update",
                refreshRateLabel: "Data Refresh Rate",
                xAxisLabel: "Time",
                yAxisLabel: "Average per Minute",
                resetBtn: "Reset Counter",
                exportBtn: "Export Data",
                dashboardBtn: "Dashboard",
                resetModalTitle: "Confirm Reset",
                resetModalMessage: "Are you sure you want to reset the footfall counter? This action cannot be undone.",
                cancelReset: "Cancel",
                confirmReset: "Confirm Reset",
                footerText: "© 2025 Intelligent Footfall Monitoring System | Real-time Data Monitoring and Analysis",
                langText: "English",
                themeText: "Dark Mode"
            }
        };

        // 全局变量
        let currentLang = 'zh';
        let isDarkMode = false;
        let chartInstance = null;
        let chartData = {
            labels: Array(30).fill('').map((_, i) => {
                const now = new Date();
                const timePoint = new Date(now.getTime() - (29 - i) * 60000);
                return timePoint.toLocaleTimeString('zh-CN', {
                    hour: '2-digit', 
                    minute: '2-digit',
                    second: '2-digit',
                    hour12: false
                });
            }),
            values: Array(30).fill(0)
        };
        let previousData = {
            totalFootfall: 0,
            averageFootfallPerMinute: 0
        };
        let startTime = new Date();
        let isFirstLoad = true;

        // DOM元素
        const totalValue = document.getElementById('total-value');
        const averageValue = document.getElementById('average-value');
        const totalTrend = document.getElementById('total-trend');
        const averageTrend = document.getElementById('average-trend');
        const sensorId = document.getElementById('sensor-id');
        const deviceIp = document.getElementById('device-ip');
        const uptime = document.getElementById('uptime');
        const connectionStatus = document.getElementById('connection-status');
        const lastUpdate = document.getElementById('last-update');
        const loadingIndicator = document.getElementById('loading-indicator');
        const resetBtn = document.getElementById('reset-btn');
        const exportBtn = document.getElementById('export-btn');
        const resetModal = document.getElementById('reset-modal');
        const cancelReset = document.getElementById('cancel-reset');
        const confirmReset = document.getElementById('confirm-reset');
        const totalCard = document.getElementById('total-card');
        const averageCard = document.getElementById('average-card');
        const langBtn = document.getElementById('lang-btn');
        const langText = document.getElementById('lang-text');
        const themeBtn = document.getElementById('theme-btn');
        const themeText = document.getElementById('theme-text');
        const htmlElement = document.getElementById('html-element');

        // 初始化图表
        function initChart() {
            const ctx = document.getElementById('footfall-chart').getContext('2d');
            chartInstance = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: chartData.labels,
                    datasets: [{
                        label: 'Footfall per Minute',
                        data: chartData.values,
                        backgroundColor: 'rgba(52, 152, 219, 0.2)',
                        borderColor: 'rgba(52, 152, 219, 1)',
                        borderWidth: 2,
                        tension: 0.4,
                        pointRadius: 3,
                        pointBackgroundColor: 'rgba(52, 152, 219, 1)',
                        fill: true
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    animation: {
                        duration: 1000,
                        easing: 'easeOutQuart'
                    },
                    scales: {
                        x: {
                            display: true,
                            title: {
                                display: true,
                                text: translations[currentLang].xAxisLabel,
                                font: {
                                    size: 14,
                                    weight: 'bold'
                                }
                            }
                        },
                        y: {
                            beginAtZero: true,
                            display: true,
                            title: {
                                display: true,
                                text: translations[currentLang].yAxisLabel,
                                font: {
                                    size: 14,
                                    weight: 'bold'
                                }
                            },
                            ticks: {
                                precision: 0
                            }
                        }
                    },
                    plugins: {
                        legend: {
                            display: false
                        },
                        tooltip: {
                            mode: 'index',
                            intersect: false,
                            callbacks: {
                                title: function(tooltipItems) {
                                    const item = tooltipItems[0];
                                    if (item.label === '0min') {
                                        return 'Current';
                                    }
                                    return item.label;
                                }
                            }
                        }
                    },
                    interaction: {
                        mode: 'nearest',
                        axis: 'x',
                        intersect: false
                    }
                }
            });
        }

        // 更新图表数据
        function updateChart(newValue) {
            // 移除最旧的数据点并添加新数据点
            chartData.values.shift();
            chartData.values.push(newValue);
            
            // 更新时间标签为当前精确时间
            const now = new Date();
            chartData.labels.shift();
            chartData.labels.push(now.toLocaleTimeString(currentLang === 'zh' ? 'zh-CN' : 'en-US', {
                hour: '2-digit', 
                minute: '2-digit',
                second: '2-digit',
                hour12: false
            }));
            
            // 更新图表
            chartInstance.data.labels = chartData.labels;
            chartInstance.data.datasets[0].data = chartData.values;
            chartInstance.update();
        }

        // 带动画效果更新数值
        function updateValueWithAnimation(element, newValue, format = 'integer') {
            const oldValue = parseFloat(element.textContent) || 0;
            
            if (oldValue !== newValue) {
                element.parentElement.classList.add('highlight');
                
                // 根据格式显示数值
                if (format === 'integer') {
                    element.textContent = Math.round(newValue);
                } else if (format === 'decimal') {
                    element.textContent = newValue.toFixed(2);
                }
                
                setTimeout(() => {
                    element.parentElement.classList.remove('highlight');
                }, 1500);
            }
        }

        // 更新趋势指示器
        function updateTrend(element, oldValue, newValue) {
            if (isFirstLoad) return;
            
            const trendIcon = element.querySelector('i');
            const trendText = element.querySelector('span');
            
            if (newValue > oldValue) {
                element.className = 'trend up';
                trendIcon.className = 'fas fa-arrow-up';
                const increase = newValue - oldValue;
                trendText.textContent = `+${increase.toFixed(2)}`;
            } else if (newValue < oldValue) {
                element.className = 'trend down';
                trendIcon.className = 'fas fa-arrow-down';
                const decrease = oldValue - newValue;
                trendText.textContent = `- ${decrease.toFixed(2)}`;
            } else {
                element.className = 'trend';
                trendIcon.className = 'fas fa-minus';
                trendText.textContent = 'No change';
            }
        }

        // 更新运行时间
        function updateUptime() {
            const now = new Date();
            const diff = Math.floor((now - startTime) / 1000);
            
            const hours = Math.floor(diff / 3600);
            const minutes = Math.floor((diff % 3600) / 60);
            const seconds = diff % 60;
            
            uptime.textContent = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
        }

        // 获取数据
        function fetchData() {
            loadingIndicator.classList.add('active');
            
            fetch('/data')
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Network response was not ok');
                    }
                    connectionStatus.innerHTML = '<span class="status-indicator" style="color: #2ecc71;">●</span> <span>Connected</span>';
                    return response.json();
                })
                .then(data => {
                    // 更新最后更新时间
                    const now = new Date();
                    lastUpdate.textContent = now.toLocaleTimeString();
                    
                    // 更新传感器信息
                    sensorId.textContent = data.sensorId;
                    
                    // 获取IP地址（假设从URL中获取）
                    if (deviceIp.textContent === '--') {
                        const url = new URL(window.location.href);
                        deviceIp.textContent = url.hostname;
                    }
                    
                    // 更新趋势
                    updateTrend(totalTrend, previousData.totalFootfall, data.totalFootfall);
                    updateTrend(averageTrend, previousData.averageFootfallPerMinute, data.averageFootfallPerMinute);
                    
                    // 更新数值（带动画）
                    updateValueWithAnimation(totalValue, data.totalFootfall, 'integer');
                    updateValueWithAnimation(averageValue, data.averageFootfallPerMinute, 'decimal');
                    
                    // 更新图表
                    updateChart(data.averageFootfallPerMinute);
                    
                    // 保存当前数据用于下次比较
                    previousData = {
                        totalFootfall: data.totalFootfall,
                        averageFootfallPerMinute: data.averageFootfallPerMinute
                    };
                    
                    isFirstLoad = false;
                })
                .catch(error => {
                    console.error('Fetch error:', error);
                    connectionStatus.innerHTML = '<span class="status-indicator" style="color: #e74c3c;">●</span> <span>Disconnected</span>';
                })
                .finally(() => {
                    loadingIndicator.classList.remove('active');
                });
        }

        // 重置计数器
        function resetCounter() {
            fetch('/reset', {
                method: 'POST'
            })
            .then(response => {
                if (response.ok) {
                    // 重置成功，刷新数据
                    fetchData();
                    // 显示成功消息
                    if (currentLang === 'zh') {
                        alert('计数器已成功重置');
                    } else {
                        alert('Counter reset successfully');
                    }
                } else {
                    throw new Error('Reset failed');
                }
            })
            .catch(error => {
                console.error('Reset error:', error);
                if (currentLang === 'zh') {
                    alert('重置失败，请重试');
                } else {
                    alert('Reset failed, please try again');
                }
            });
        }

        // 导出数据为CSV
        function exportData() {
            // 创建CSV内容
            const csvContent = [
                'Time,Sensor ID,Total Footfall,Average Footfall per Minute',
                `${new Date().toLocaleString()},${sensorId.textContent},${totalValue.textContent},${averageValue.textContent}`
            ].join('\n');
            
            // 创建下载链接
            const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
            const url = URL.createObjectURL(blob);
            const link = document.createElement('a');
            link.setAttribute('href', url);
            link.setAttribute('download', `Footfall_Data_${new Date().toISOString().slice(0,10)}.csv`);
            link.style.visibility = 'hidden';
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
        }

        // 更新当前日期
        function updateCurrentDate() {
            const now = new Date();
            const dateElement = document.getElementById('current-date');
            
            if (currentLang === 'zh') {
                const options = { 
                    year: 'numeric', 
                    month: 'long', 
                    day: 'numeric',
                    weekday: 'short'
                };
                dateElement.textContent = now.toLocaleDateString('zh-CN', options);
            } else {
                const options = { 
                    year: 'numeric', 
                    month: 'short', 
                    day: 'numeric',
                    weekday: 'short'
                };
                dateElement.textContent = now.toLocaleDateString('en-US', options);
            }
        }

        // 切换语言
        function switchLanguage(lang) {
    currentLang = lang;
    
    // 更新页面文本
    document.getElementById('page-title').textContent = translations[lang].pageTitle;
    document.getElementById('page-subtitle').textContent = translations[lang].pageSubtitle;
    document.getElementById('total-card-title').querySelector('span').textContent = translations[lang].totalCardTitle;
    document.getElementById('average-card-title').querySelector('span').textContent = translations[lang].averageCardTitle;
    document.getElementById('sensor-info-title').textContent = translations[lang].sensorInfoTitle;
    document.getElementById('sensor-id-label').textContent = translations[lang].sensorIdLabel;
    document.getElementById('device-ip-label').textContent = translations[lang].deviceIpLabel;
    document.getElementById('uptime-label').textContent = translations[lang].uptimeLabel;
    document.getElementById('system-status-title').textContent = translations[lang].systemStatusTitle;
    document.getElementById('connection-status-label').textContent = translations[lang].connectionStatusLabel;
    document.getElementById('last-update-label').textContent = translations[lang].lastUpdateLabel;
    document.getElementById('refresh-rate-label').textContent = translations[lang].refreshRateLabel;
    
    // 更新按钮文本
    document.getElementById('reset-modal-title').textContent = translations[lang].resetModalTitle;
    document.getElementById('dashboard-text').textContent = translations[lang].dashboardBtn;
    
    // 更新当前日期
    updateCurrentDate();
    
    // 只更新图表轴标签，不重新生成时间标签数组
    if (chartInstance) {
        chartInstance.options.scales.x.title.text = translations[lang].xAxisLabel;
        chartInstance.options.scales.y.title.text = translations[lang].yAxisLabel;
        chartInstance.update();
    }
    
    // 更新语言按钮文本
    document.querySelector('.lang-btn').textContent = translations[lang].langText;
}

        // 切换主题
        function switchTheme() {
            isDarkMode = !isDarkMode;
            if (isDarkMode) {
                htmlElement.classList.add('dark');
                themeBtn.innerHTML = '<i class="fas fa-sun"></i> <span>亮色模式</span>';
                if (currentLang === 'zh') {
                    themeText.textContent = "亮色模式";
                } else {
                    themeText.textContent = "Light Mode";
                }
            } else {
                htmlElement.classList.remove('dark');
                themeBtn.innerHTML = '<i class="fas fa-moon"></i> <span>暗色模式</span>';
                if (currentLang === 'zh') {
                    themeText.textContent = "暗色模式";
                } else {
                    themeText.textContent = "Dark Mode";
                }
            }
        }

        // 事件监听器
        resetBtn.addEventListener('click', () => {
            resetModal.style.display = 'flex';
        });

        cancelReset.addEventListener('click', () => {
            resetModal.style.display = 'none';
        });

        confirmReset.addEventListener('click', () => {
            resetModal.style.display = 'none';
            resetCounter();
        });

        exportBtn.addEventListener('click', exportData);

        // 仪表板按钮点击事件
        const dashboardBtn = document.getElementById('dashboard-btn');
        dashboardBtn.addEventListener('click', () => {
            window.open('https://io.adafruit.com/yihequanming/dashboards/intelligent-crowd-flow-detection', '_blank');
        });

        langBtn.addEventListener('click', () => {
            if (currentLang === 'zh') {
                switchLanguage('en');
            } else {
                switchLanguage('zh');
            }
        });

        themeBtn.addEventListener('click', switchTheme);

        // 点击其他区域关闭模态框
        window.addEventListener('click', (event) => {
            if (event.target === resetModal) {
                resetModal.style.display = 'none';
            }
        });

        // 初始化
        document.addEventListener('DOMContentLoaded', () => {
            initChart();
            fetchData(); // 初始获取数据
            setInterval(fetchData, 2000); // 每2秒更新一次数据
            setInterval(updateUptime, 1000); // 每秒更新运行时间
            updateCurrentDate(); // 初始化日期显示
            setInterval(updateCurrentDate, 60000); // 每分钟更新一次日期
            
            // 检查本地存储中的主题偏好
            const savedTheme = localStorage.getItem('theme');
            if (savedTheme === 'dark') {
                isDarkMode = true;
                switchTheme();
            }
            
            // 保存主题偏好
            themeBtn.addEventListener('change', () => {
                localStorage.setItem('theme', isDarkMode ? 'dark' : 'light');
            });
        });
    </script>
</body>
</html>
)rawliteral";

// 新增：处理根路径 ("/") 的请求，使用优化后的HTML
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

// 新增：处理获取数据的请求 ("/data")
void handleGetData() {
  String json = "{";
  json += "\"sensorId\":\"" + String(sensorId) + "\",";
  json += "\"totalFootfall\":" + String(footfallCount) + ",";
  json += "\"averageFootfallPerMinute\":" + String(averageFootfallPerMinute, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// 新增：处理重置计数器的请求 ("/reset")
void handleResetCounter() {
  if (server.method() == HTTP_POST) {
    footfallCount = 0;
    currentSecondFootfall = 0; // 同时重置当前秒的计数
    // 重置分钟数据数组，以确保平均值也重置
    for (int i = 0; i < 60; i++) {
      lastMinuteFootfall[i] = 0;
    }
    averageFootfallPerMinute = 0.0;
    Serial.println("计数器已通过HTTP重置");
    // 重定向回根页面
    server.sendHeader("Location", "/");
    server.send(303); // 303 See Other 状态码表示重定向，并建议使用GET方法获取新资源
    updateLcdDisplay(); // 立即更新LCD显示
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// MQTT连接与重连函数
void MQTT_connect() {
  int8_t ret;
  if (mqtt.connected()) return;
  
  Serial.print("连接到MQTT服务器...");
  uint8_t retries = 3;
  
  while ((ret = mqtt.connect()) != 0) {
    Serial.printf("MQTT连接失败: %s\n", mqtt.connectErrorString(ret));
    Serial.println("5秒后重试...");
    mqtt.disconnect();
    delay(5000);
    retries--;
    if (retries == 0) {
      Serial.println("MQTT连接失败，将在下次发布时重试");
      return;  // 改为return，而不是死循环
    }
  }
  Serial.println("MQTT连接成功!");
}

// 发布客流量数据
void publishFootfallData() {
  if (!mqtt.connected()) {
    MQTT_connect();
    if (!mqtt.connected()) return;
  }
  
  String footfallStr = String(footfallCount);
  if (footfallPublish.publish(footfallStr.c_str())) {
    Serial.printf("已发布总客流量: %s\n", footfallStr.c_str());
  } else {
    Serial.println("发布总客流量失败");
  }
  
  String averageStr = String(averageFootfallPerMinute, 1);
  if (averagePublish.publish(averageStr.c_str())) {
    Serial.printf("已发布平均客流量: %s\n", averageStr.c_str());
  } else {
    Serial.println("发布平均客流量失败");
  }
  
  // 构建Current information
  String currentInfo = "当前IP: " + WiFi.localIP().toString() + "\n";
  currentInfo += "MQTT连接: 成功\n";
  currentInfo += "传感器ID: footfall_sensor_001";
  
  // 只在信息发生变化或首次发布时才发送
  if (!currentInfoPublished || currentInfo != lastPublishedCurrentInfo) {
    if (currentInfoPublish.publish(currentInfo.c_str())) {
      Serial.printf("已发布当前信息: %s\n", currentInfo.c_str());
      lastPublishedCurrentInfo = currentInfo;
      currentInfoPublished = true;
    } else {
      Serial.println("发布当前信息失败");
    }
  }
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.setRGB(0, 0, 255);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  connectToWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleGetData);
    server.on("/reset", HTTP_POST, handleResetCounter);
    server.begin();
    Serial.println("HTTP server started");
    MQTT_connect();
  } else {
    Serial.println("WiFi connection failed. HTTP server not started.");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi Failed");
    lcd.setCursor(0,1);
    lcd.print("Server Offline"); 
  }

  buttonPoller.attach_ms(5, pollButton);
  lcdUpdater.attach(1, updateLcdDisplay);
  minuteFootfallCalculator.attach(1, updateFootfallData);
  updateLcdDisplay();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
    
    unsigned long currentTime = millis();
    if (currentTime - lastMqttPublishTime >= mqttPublishInterval) {
      publishFootfallData();
      lastMqttPublishTime = currentTime;
    }
  } else {
    Serial.println("Wi-Fi 连接断开，尝试重新连接...");
    digitalWrite(LED_PIN, LOW);
    connectToWiFi();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected. Server should be accessible.");
      // WiFi重连后重置Current info发布状态，因为IP可能变化
      currentInfoPublished = false;
      MQTT_connect();
    }
  }
}

// 按钮轮询函数
void pollButton() {
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    footfallCount++;
    currentSecondFootfall++;
    Serial.print("检测到一次低到高转换 (访客通过)，当前总人数: ");
    Serial.println(footfallCount);
  }
  lastButtonState = currentButtonState;
}

// 每秒更新客流量数据并计算平均值
void updateFootfallData() {
  lastMinuteFootfall[minuteFootfallIndex] = currentSecondFootfall;
  currentSecondFootfall = 0;
  minuteFootfallIndex = (minuteFootfallIndex + 1) % 60;

  int totalLastMinuteFootfall = 0;
  for (int i = 0; i < 60; i++) {
    totalLastMinuteFootfall += lastMinuteFootfall[i];
  }

  averageFootfallPerMinute = (float)totalLastMinuteFootfall;
  Serial.print("每分钟平均客流量: ");
  Serial.println(averageFootfallPerMinute);
  
  // 添加IP地址和总人数的打印
  Serial.print("设备IP地址: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP().toString());
  } else {
    Serial.println("未连接WiFi");
  }
  Serial.print("当前总人数: ");
  Serial.println(footfallCount);
  Serial.println("-------------------"); // 分隔线，便于阅读
}

// 更新LCD显示
void updateLcdDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print(WiFi.localIP().toString());
  } else {
    lcd.print("No WiFi");
  }

  lcd.setCursor(0, 1);
  lcd.print("Avg/min: ");
  lcd.print((int)averageFootfallPerMinute);
}

// 连接到WiFi
void connectToWiFi() {
  Serial.print("正在连接到 Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  digitalWrite(LED_PIN, LOW);
  lcd.setRGB(255, 165, 0);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi 已连接");
    Serial.print("IP 地址: ");
    Serial.println(WiFi.localIP());
    lcd.setRGB(0, 255, 0);
    digitalWrite(LED_PIN, HIGH);
    updateLcdDisplay();
  } else {
    Serial.println("\nWi-Fi 连接失败");
    lcd.setRGB(255, 0, 0);
    digitalWrite(LED_PIN, LOW);
  }
}
