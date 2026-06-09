#include "web/web_server.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <vector>
#include <sstream>
#include "storage/session_store.hpp"
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET closesocket
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define SOCKET_ERROR_VAL SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET close
#define INVALID_SOCKET_VAL -1
#define SOCKET_ERROR_VAL -1
#endif

// HTML Dashboard content
const char* DASHBOARD_HTML = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LLMScope Observability Dashboard</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
    <style>
        body { font-family: 'Inter', sans-serif; }
        .mono { font-family: 'JetBrains Mono', monospace; }
        .glass { background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(12px); border: 1px solid rgba(255, 255, 255, 0.05); }
        /* Custom scrollbar for Web Dashboard */
        ::-webkit-scrollbar { width: 6px; height: 6px; }
        ::-webkit-scrollbar-track { background: rgba(15, 23, 42, 0.3); }
        ::-webkit-scrollbar-thumb { background: rgba(20, 184, 166, 0.3); border-radius: 3px; }
        ::-webkit-scrollbar-thumb:hover { background: rgba(20, 184, 166, 0.6); }
    </style>
</head>
<body class="bg-slate-950 text-slate-100 min-h-screen pb-12">
    <!-- Header -->
    <header class="border-b border-slate-800 bg-slate-900/50 backdrop-blur sticky top-0 z-50 px-6 py-4 flex items-center justify-between">
        <div class="flex items-center space-x-3">
            <span class="text-2xl font-bold bg-gradient-to-r from-emerald-400 to-teal-500 bg-clip-text text-transparent">LLMSCOPE</span>
            <span class="text-xs px-2 py-0.5 rounded-full border border-teal-500/30 text-teal-400 bg-teal-500/10 font-semibold uppercase tracking-wider">Web Dashboard</span>
        </div>
        <div class="flex items-center space-x-6 text-sm">
            <div id="device-badge" class="glass px-3 py-1.5 rounded-lg text-slate-300 font-medium">Device: <span id="device-mode-text" class="text-teal-400">Loading...</span></div>
            <div id="model-badge" class="glass px-3 py-1.5 rounded-lg text-slate-300 font-medium">Model: <span class="text-emerald-400">Loading...</span></div>
            <div id="status-badge" class="flex items-center space-x-2 text-emerald-400 font-semibold">
                <span class="w-2 h-2 rounded-full bg-emerald-400 animate-ping"></span>
                <span id="live-feed-text">LIVE FEED</span>
            </div>
        </div>
    </header>

    <main class="max-w-7xl mx-auto px-6 mt-8 space-y-8">
        <!-- Key Metrics Row -->
        <section class="grid grid-cols-1 md:grid-cols-4 gap-6">
            <div class="glass p-6 rounded-2xl flex flex-col justify-between shadow-xl">
                <span class="text-slate-400 text-xs uppercase tracking-wider font-semibold">Tokens / Sec</span>
                <span id="stat-tps" class="text-3xl font-bold text-teal-400 mt-2 mono">0.00</span>
            </div>
            <div class="glass p-6 rounded-2xl flex flex-col justify-between shadow-xl">
                <span class="text-slate-400 text-xs uppercase tracking-wider font-semibold">Avg Latency</span>
                <span id="stat-latency" class="text-3xl font-bold text-teal-400 mt-2 mono">0.0 ms</span>
            </div>
            <div class="glass p-6 rounded-2xl flex flex-col justify-between shadow-xl">
                <span class="text-slate-400 text-xs uppercase tracking-wider font-semibold">Events Processed</span>
                <span id="stat-events" class="text-3xl font-bold text-slate-300 mt-2 mono">0</span>
            </div>
            <div class="glass p-6 rounded-2xl border-rose-500/20 bg-rose-950/10 flex flex-col justify-between shadow-xl">
                <span class="text-rose-400 text-xs uppercase tracking-wider font-semibold">Numerical Anomalies</span>
                <span id="stat-anomalies" class="text-3xl font-bold text-rose-400 mt-2 mono">0</span>
            </div>
        </section>

        <!-- Session Summary & Replay Controls Row -->
        <section class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <!-- Session Summary Card -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col justify-between">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Session Summary</h2>
                <div class="space-y-2 text-xs mono">
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Model Name:</span>
                        <span id="summary-model" class="text-emerald-400 font-bold">-</span>
                    </div>
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Events:</span>
                        <span id="summary-events" class="text-teal-400 font-bold">0</span>
                    </div>
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Avg Latency:</span>
                        <span id="summary-latency" class="text-teal-400 font-bold">0.0 ms</span>
                    </div>
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Tokens/Sec:</span>
                        <span id="summary-tps" class="text-teal-400 font-bold">0.00</span>
                    </div>
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Anomalies:</span>
                        <span id="summary-anomalies" class="text-rose-400 font-bold">0</span>
                    </div>
                    <div class="flex justify-between border-b border-slate-800 pb-1">
                        <span>Worst Layer:</span>
                        <span id="summary-worst" class="text-yellow-400 font-bold">-</span>
                    </div>
                    <div class="flex justify-between">
                        <span>Peak Memory:</span>
                        <span id="summary-memory" class="text-emerald-400 font-bold">2.4 GB</span>
                    </div>
                </div>
            </div>

            <!-- Replay Control Panel -->
            <div id="replay-panel" class="glass p-6 rounded-2xl shadow-xl flex flex-col justify-between lg:col-span-2">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Replay Timeline & Controls</h2>
                <div class="flex flex-col space-y-4">
                    <div class="flex items-center justify-between">
                        <div class="flex items-center space-x-2">
                            <span id="replay-badge" class="text-[10px] px-2 py-0.5 rounded-full border border-slate-700 text-slate-400 bg-slate-800/50 font-bold uppercase tracking-wider">Live Mode</span>
                            <span id="replay-status-text" class="text-xs text-slate-400 font-medium">Monitoring events...</span>
                        </div>
                        <!-- Custom visual scrubber [=====|----------] -->
                        <div class="mono text-xs text-teal-400 font-bold bg-slate-950/50 px-3 py-1.5 rounded-lg border border-slate-800" id="replay-visual-scrubber">
                            [ ]
                        </div>
                    </div>
                    <!-- Range Input Scrubber -->
                    <div class="flex items-center space-x-2">
                        <span class="text-[10px] text-slate-400 mono" id="replay-index-start">0</span>
                        <input type="range" id="replay-scrubber" min="0" max="0" value="0" class="flex-1 accent-teal-400 bg-slate-850 rounded-lg h-1.5 cursor-pointer" disabled>
                        <span class="text-[10px] text-slate-400 mono" id="replay-index-end">0</span>
                    </div>
                    <!-- Playback buttons -->
                    <div class="flex items-center justify-center space-x-3">
                        <button id="btn-replay-prev" class="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-300 rounded-lg text-xs font-semibold border border-slate-700 transition" disabled>◀ Step Back</button>
                        <button id="btn-replay-play" class="px-4 py-1.5 bg-teal-500 hover:bg-teal-400 text-slate-950 font-bold rounded-lg text-xs transition shadow-lg shadow-teal-500/10" disabled>▶ Play</button>
                        <button id="btn-replay-pause" class="px-4 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-300 rounded-lg text-xs font-semibold border border-slate-700 transition" disabled>⏸ Pause</button>
                        <button id="btn-replay-next" class="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-300 rounded-lg text-xs font-semibold border border-slate-700 transition" disabled>Step Forward ▶</button>
                        <button id="btn-replay-live" class="px-3 py-1.5 bg-rose-950/35 hover:bg-rose-900/50 text-rose-400 rounded-lg text-xs font-semibold border border-rose-900/50 transition" disabled>Go to Live ⬤</button>
                    </div>
                    <!-- Export & Save buttons -->
                    <div class="flex items-center justify-center space-x-3 border-t border-slate-800/80 pt-3 mt-1">
                        <button id="btn-export-json" class="px-3 py-1.5 bg-slate-850 hover:bg-slate-750 text-slate-300 rounded-lg text-[11px] font-semibold border border-slate-700 transition">📥 Export JSON</button>
                        <button id="btn-export-csv" class="px-3 py-1.5 bg-slate-850 hover:bg-slate-750 text-slate-300 rounded-lg text-[11px] font-semibold border border-slate-700 transition">📥 Export CSV</button>
                        <button id="btn-save-session" class="px-3 py-1.5 bg-emerald-950/30 hover:bg-emerald-900/40 text-emerald-400 rounded-lg text-[11px] font-semibold border border-emerald-900/40 transition">💾 Save Session (Server)</button>
                    </div>
                </div>
            </div>
        </section>

        <!-- Charts Grid -->
        <section class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <!-- Latency Profiler -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col justify-between">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Real-Time Execution Latency (ms)</h2>
                <div class="h-64"><canvas id="latencyChart"></canvas></div>
            </div>
            <!-- Device Placements -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col justify-between">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Compute Device Allocations</h2>
                <div class="h-64 flex justify-center"><canvas id="deviceChart"></canvas></div>
            </div>
            <!-- Top Slowest Layers -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col justify-between">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Top Slowest Layers</h2>
                <div class="h-64 overflow-y-auto space-y-2 text-xs pr-1" id="slowest-layers-list">
                    <div class="text-slate-500 text-center py-8">Analyzing layer execution speeds...</div>
                </div>
            </div>
        </section>

        <!-- Dynamic Observation Panels -->
        <section class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <!-- Model Topology Tree -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col max-h-[500px]">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Model Topology Explorer</h2>
                <div class="overflow-y-auto flex-1 text-xs mono space-y-1 pr-1" id="topology-tree">
                    <div class="text-slate-500 text-center py-8">Waiting for model metadata...</div>
                </div>
            </div>

            <!-- Live Event Log -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col max-h-[500px]">
                <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Live Telemetry Packet Stream</h2>
                <div class="overflow-y-auto flex-1 border border-slate-800 rounded-lg">
                    <table class="w-full text-left border-collapse">
                        <thead>
                            <tr class="bg-slate-900/80 text-xs text-slate-400 uppercase border-b border-slate-800 font-semibold sticky top-0">
                                <th class="p-3">ID</th>
                                <th class="p-3">Layer</th>
                                <th class="p-3">Type</th>
                                <th class="p-3">Device</th>
                                <th class="p-3">Latency</th>
                            </tr>
                        </thead>
                        <tbody id="stream-table-body" class="text-xs mono">
                            <tr>
                                <td colspan="5" class="p-4 text-center text-slate-500">Waiting for model trace packets...</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <!-- Anomaly Ledger -->
            <div class="glass p-6 rounded-2xl shadow-xl flex flex-col max-h-[500px]">
                <h2 class="text-sm uppercase tracking-wider font-bold text-rose-400 mb-4">Active Numerical Anomalies</h2>
                
                <!-- Anomaly Summary Counters -->
                <div class="grid grid-cols-4 gap-2 mb-4 text-center text-[10px] uppercase font-bold text-rose-400/90" id="anomaly-summary-counters">
                    <div class="bg-rose-950/20 border border-rose-500/10 p-2 rounded-xl">
                        <div>NaN/Inf</div>
                        <div class="text-base font-bold mt-1 text-rose-400 mono" id="count-nan">0</div>
                    </div>
                    <div class="bg-rose-950/20 border border-rose-500/10 p-2 rounded-xl">
                        <div>Dead</div>
                        <div class="text-base font-bold mt-1 text-rose-400 mono" id="count-dead">0</div>
                    </div>
                    <div class="bg-rose-950/20 border border-rose-500/10 p-2 rounded-xl">
                        <div>Explode</div>
                        <div class="text-base font-bold mt-1 text-rose-400 mono" id="count-explode">0</div>
                    </div>
                    <div class="bg-rose-950/20 border border-rose-500/10 p-2 rounded-xl">
                        <div>CPU Fall</div>
                        <div class="text-base font-bold mt-1 text-rose-400 mono" id="count-fallback">0</div>
                    </div>
                </div>

                <div class="overflow-y-auto flex-1 border border-rose-500/10 rounded-lg bg-rose-950/5 p-4 space-y-3" id="anomaly-list">
                    <div class="text-xs text-slate-500 text-center py-8">No anomalies detected in the current run.</div>
                </div>
            </div>
        </section>

        <!-- Comparative Session Analysis -->
        <section class="glass p-6 rounded-2xl shadow-xl">
            <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4 flex items-center space-x-2">
                <span>Comparative Session Analysis</span>
                <span class="text-[10px] px-2 py-0.5 rounded-full border border-teal-500/30 text-teal-400 bg-teal-500/10 font-semibold uppercase">Benchmark Tool</span>
            </h2>
            <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
                <!-- Inputs & Controls -->
                <div class="space-y-4">
                    <p class="text-xs text-slate-400">
                        Perform side-by-side run evaluations by uploading exported session JSON files. Compare different quantization levels (e.g. Q4_K_M vs FP16) or different hardware/latency runs.
                    </p>
                    <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
                        <div class="bg-slate-900/50 border border-slate-800 p-4 rounded-xl">
                            <label class="text-[10px] text-teal-400 font-bold block mb-2 uppercase tracking-wider">Run A (Reference Baseline)</label>
                            <input type="file" id="compare-file-a" accept=".json" class="w-full text-xs text-slate-400 file:mr-2 file:py-1 file:px-2.5 file:rounded-md file:border-0 file:text-xs file:font-semibold file:bg-teal-500/10 file:text-teal-400 hover:file:bg-teal-500/20 cursor-pointer">
                            <button id="btn-use-current-a" class="mt-3 w-full px-2.5 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-300 rounded-lg text-[10px] font-semibold border border-slate-700 transition">Use Current Run as Run A</button>
                        </div>
                        <div class="bg-slate-900/50 border border-slate-800 p-4 rounded-xl">
                            <label class="text-[10px] text-emerald-400 font-bold block mb-2 uppercase tracking-wider">Run B (Comparison Target)</label>
                            <input type="file" id="compare-file-b" accept=".json" class="w-full text-xs text-slate-400 file:mr-2 file:py-1 file:px-2.5 file:rounded-md file:border-0 file:text-xs file:font-semibold file:bg-emerald-500/10 file:text-emerald-400 hover:file:bg-emerald-500/20 cursor-pointer">
                            <button id="btn-compare-clear" class="mt-3 w-full px-2.5 py-1.5 bg-slate-800 hover:bg-slate-750 text-rose-400 rounded-lg text-[10px] font-semibold border border-slate-700 transition">Clear Uploaded Runs</button>
                        </div>
                    </div>
                </div>
                <!-- Benchmarks Matrix Table -->
                <div class="border border-slate-800 rounded-xl bg-slate-950 p-4 overflow-x-auto">
                    <table class="w-full text-left border-collapse text-xs mono">
                        <thead>
                            <tr class="text-slate-400 border-b border-slate-800/80 uppercase font-semibold text-[10px] tracking-wider">
                                <th class="pb-3 pr-4">Performance Metric</th>
                                <th class="pb-3 pr-4">Run A</th>
                                <th class="pb-3 pr-4">Run B</th>
                                <th class="pb-3">Delta Analysis</th>
                            </tr>
                        </thead>
                        <tbody id="comparison-table-body">
                            <tr>
                                <td colspan="4" class="text-slate-500 text-center py-8">Please upload session JSON files or click 'Use Current Run' to analyze comparison.</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </section>

        <!-- Attention Matrix View -->
        <section class="glass p-6 rounded-2xl shadow-xl">
            <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4">Attention Matrix Visualizer</h2>
            <div class="flex flex-col md:flex-row gap-6">
                <div class="flex-1 max-w-md space-y-4">
                    <div>
                        <label class="text-xs text-slate-400 font-semibold block mb-1">Select Layer</label>
                        <select id="attn-layer" class="w-full bg-slate-800 border border-slate-700 rounded-lg p-2.5 text-xs text-slate-200 focus:outline-none focus:border-teal-500">
                            <option>No layers loaded</option>
                        </select>
                    </div>
                    <div>
                        <label class="text-xs text-slate-400 font-semibold block mb-1">Select Head</label>
                        <select id="attn-head" class="w-full bg-slate-800 border border-slate-700 rounded-lg p-2.5 text-xs text-slate-200 focus:outline-none focus:border-teal-500">
                            <option value="0">Head 0</option>
                        </select>
                    </div>
                    <div class="text-xs text-slate-400 space-y-2 bg-slate-900/50 p-3 rounded-lg border border-slate-800">
                        <p class="font-bold text-slate-300 border-b border-slate-800 pb-1">Attention Metrics</p>
                        <p class="flex justify-between"><span>Average Entropy:</span> <span id="attn-entropy" class="text-emerald-400 font-bold">-</span></p>
                    </div>
                </div>
                <div class="flex-1 flex justify-center bg-slate-950 border border-slate-850 p-6 rounded-2xl shadow-inner">
                    <canvas id="attnCanvas" width="300" height="300" class="max-w-full rounded-lg bg-slate-950"></canvas>
                </div>
            </div>
        </section>

        <!-- Token Semantic Explorer View -->
        <section class="glass p-6 rounded-2xl shadow-xl">
            <h2 class="text-sm uppercase tracking-wider font-bold text-slate-300 mb-4 flex items-center justify-between">
                <span>Token Semantic Explorer (Nearest Neighbor Analysis)</span>
                <span class="text-[10px] px-2 py-0.5 rounded-full border border-teal-500/30 text-teal-400 bg-teal-500/10 font-semibold uppercase">Interpretability Module</span>
            </h2>
            <div class="flex flex-col lg:flex-row gap-6">
                <!-- Selector Side -->
                <div class="flex-1 max-w-md space-y-4">
                    <div>
                        <label class="text-xs text-slate-400 font-semibold block mb-1">Select Layer</label>
                        <select id="semantic-layer" class="w-full bg-slate-800 border border-slate-700 rounded-lg p-2.5 text-xs text-slate-200 focus:outline-none focus:border-teal-500">
                            <option>No layers loaded</option>
                        </select>
                    </div>
                    <div>
                        <label class="text-xs text-slate-400 font-semibold block mb-1">Select Token</label>
                        <select id="semantic-token" class="w-full bg-slate-800 border border-slate-700 rounded-lg p-2.5 text-xs text-slate-200 focus:outline-none focus:border-teal-500">
                            <option>No tokens available</option>
                        </select>
                    </div>
                    <div class="text-[11px] text-slate-400 bg-slate-900/50 p-3 rounded-lg border border-slate-800 space-y-1">
                        <p class="font-bold text-slate-300">💡 Navigation Tips:</p>
                        <p>• Use <b>ArrowLeft / ArrowRight</b> keys to transition between transformer layers (0 - 31).</p>
                        <p>• Click tokens directly to inspect their embedding space neighborhood.</p>
                    </div>
                </div>
                <!-- Nearest Neighbors Display Side -->
                <div class="flex-1 border border-slate-800 rounded-xl bg-slate-950 p-4 min-h-[300px] flex flex-col">
                    <h3 class="text-xs font-bold text-teal-400 uppercase tracking-wider mb-3 pb-2 border-b border-slate-800">Top 10 Nearest Neighbors in Vocabulary Embedding Space</h3>
                    <div id="semantic-neighbors-list" class="flex-1 space-y-2.5 overflow-y-auto max-h-[350px] pr-1">
                        <div class="text-slate-500 text-center py-16 text-xs">Select a layer and token to inspect semantic neighbors.</div>
                    </div>
                </div>
            </div>
        </section>
    </main>

    <script>
        // Init Charts
        const ctxL = document.getElementById('latencyChart').getContext('2d');
        const latencyChart = new Chart(ctxL, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Submodule Latency (ms)',
                    data: [],
                    borderColor: 'rgb(20, 184, 166)',
                    backgroundColor: 'rgba(20, 184, 166, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { ticks: { color: '#94a3b8', font: { size: 10 } }, grid: { color: 'rgba(255,255,255,0.05)' } },
                    y: { ticks: { color: '#94a3b8' }, grid: { color: 'rgba(255,255,255,0.05)' } }
                },
                plugins: { legend: { display: false } }
            }
        });

        const ctxD = document.getElementById('deviceChart').getContext('2d');
        const deviceChart = new Chart(ctxD, {
            type: 'doughnut',
            data: {
                labels: ['CUDA', 'CPU'],
                datasets: [{
                    data: [0, 0],
                    backgroundColor: ['#14b8a6', '#64748b'],
                    borderWidth: 1,
                    borderColor: '#0f172a'
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { labels: { color: '#cbd5e1' } } }
            }
        });

        let loadedLayers = new Set();
        let attentionCache = {};
        let semanticCache = {};
        let semanticLayersLoaded = new Set();
        let topologyRendered = false;
        let layersExpanded = false;

        function updateStats() {
            fetch('/api/stats')
                .then(res => res.json())
                .then(data => {
                    document.getElementById('stat-tps').innerText = data.tokens_per_sec.toFixed(2);
                    document.getElementById('stat-latency').innerText = data.avg_inference_latency.toFixed(1) + " ms";
                    document.getElementById('stat-events').innerText = data.events_processed;
                    
                    // Session Summary Card updates
                    document.getElementById('summary-events').innerText = data.events_processed;
                    document.getElementById('summary-latency').innerText = data.avg_inference_latency.toFixed(1) + " ms";
                    document.getElementById('summary-tps').innerText = data.tokens_per_sec.toFixed(2);
                    
                    if (data.slowest_layers && data.slowest_layers.length > 0) {
                        document.getElementById('summary-worst').innerText = data.slowest_layers[0].name;
                    } else {
                        document.getElementById('summary-worst').innerText = "-";
                    }
                    
                    if (data.gpu_available) {
                        document.getElementById('summary-memory').innerText = data.vram_used_gb.toFixed(1) + " / " + data.vram_total_gb.toFixed(1) + " GB";
                    } else {
                        document.getElementById('summary-memory').innerText = data.ram_used_gb.toFixed(1) + " / " + data.ram_total_gb.toFixed(1) + " GB";
                    }
                    
                    // Hardware stats representation & device chart updates
                    if (data.gpu_available) {
                        document.getElementById('device-mode-text').innerHTML = 
                            `<span class="text-teal-400">CUDA Available</span> (${data.gpu_name})`;
                        
                        // Dynamically resolve device distributions
                        let cuda_count = 0;
                        let cpu_count = 0;
                        for (const key in data.devices) {
                            if (key.includes('CUDA')) {
                                cuda_count += data.devices[key];
                            } else if (key.includes('CPU')) {
                                cpu_count += data.devices[key];
                            }
                        }
                        deviceChart.data.datasets[0].data = [cuda_count, cpu_count];
                    } else {
                        document.getElementById('device-mode-text').innerHTML = 
                            `<span class="text-slate-400">CPU-Only Mode</span>`;
                        // CPU-only mode: Show 100% CPU
                        deviceChart.data.datasets[0].data = [0, 100];
                    }
                    deviceChart.update();

                    // Update slowest layers list
                    const slowEl = document.getElementById('slowest-layers-list');
                    if (data.slowest_layers && data.slowest_layers.length > 0) {
                        slowEl.innerHTML = data.slowest_layers.map((l, idx) => `
                            <div class="flex items-center justify-between p-2 rounded-lg bg-slate-900/50 border border-slate-800 text-xs">
                                <span class="font-semibold text-slate-350">${idx+1}. <span class="text-teal-400 font-bold">${l.name}</span> <span class="text-slate-500 font-normal">(${l.type})</span></span>
                                <span class="mono text-teal-400 font-bold">${l.avg_latency_ms.toFixed(1)} ms</span>
                            </div>
                        `).join('');
                    } else {
                        slowEl.innerHTML = '<div class="text-slate-500 text-center py-8">Analyzing layer execution speeds...</div>';
                    }
                }).catch(e => console.error(e));

            fetch('/api/anomalies')
                .then(res => res.json())
                .then(alerts => {
                    document.getElementById('stat-anomalies').innerText = alerts.length;
                    document.getElementById('summary-anomalies').innerText = alerts.length;
                    
                    // Update anomaly summary counters
                    let nans = 0, dead = 0, explode = 0, fallback = 0;
                    alerts.forEach(a => {
                        const desc = a.description.toLowerCase();
                        if (desc.includes("nan") || desc.includes("inf")) nans++;
                        else if (desc.includes("dead") || desc.includes("variance")) dead++;
                        else if (desc.includes("explode") || desc.includes("exploding")) explode++;
                        else if (desc.includes("fallback")) fallback++;
                    });
                    document.getElementById('count-nan').innerText = nans;
                    document.getElementById('count-dead').innerText = dead;
                    document.getElementById('count-explode').innerText = explode;
                    document.getElementById('count-fallback').innerText = fallback;

                    const listEl = document.getElementById('anomaly-list');
                    if (alerts.length === 0) {
                        listEl.innerHTML = '<div class="text-xs text-slate-500 text-center py-8">No anomalies detected in the current run.</div>';
                    } else {
                        listEl.innerHTML = alerts.map(a => `
                            <div class="flex flex-col p-3 rounded-lg bg-rose-500/10 border border-rose-500/20 text-xs">
                                <div class="flex items-center justify-between font-bold text-rose-400 mb-1">
                                    <span>${a.severity} in ${a.layer_name}</span>
                                    <span>${a.timestamp}</span>
                                </div>
                                <div class="text-slate-300">${a.description}</div>
                            </div>
                        `).join('');
                    }
                }).catch(e => console.error(e));

            fetch('/api/events')
                .then(res => res.json())
                .then(events => {
                    if (events.length === 0) return;
                    
                    // Filter layer traces
                    const traces = events.filter(e => e.event_type === 'layer_trace');
                    const modelInfo = events.find(e => e.event_type === 'model_info');
                    const attns = events.filter(e => e.event_type === 'attention_weights');
                    
                    if (modelInfo) {
                        document.getElementById('model-badge').querySelector('span').innerText = 
                            `${modelInfo.payload.name} (${modelInfo.payload.quantization})`;
                        document.getElementById('summary-model').innerText = 
                            `${modelInfo.payload.name} (${modelInfo.payload.quantization})`;
                        
                        if (!topologyRendered) {
                            renderTopologyTree(modelInfo.payload.name, modelInfo.payload.layers);
                            topologyRendered = true;
                        }
                    }

                    // Update Latency line chart
                    const chartData = traces.slice(-30);
                    latencyChart.data.labels = chartData.map(e => e.payload.layer_name.split('.').slice(-2).join('.'));
                    latencyChart.data.datasets[0].data = chartData.map(e => e.payload.latency_ms);
                    latencyChart.update();

                    // Update streams table
                    const tableBody = document.getElementById('stream-table-body');
                    tableBody.innerHTML = traces.slice(-15).reverse().map(e => {
                        const devColor = e.payload.device.includes('CUDA') ? 'text-teal-400' : 'text-slate-400';
                        return `
                            <tr class="border-b border-slate-900 hover:bg-slate-900/40 transition">
                                <td class="p-3 text-slate-500">${e.payload.event_id}</td>
                                <td class="p-3 font-semibold text-slate-200">${e.payload.layer_name}</td>
                                <td class="p-3 text-slate-400">${e.payload.layer_type}</td>
                                <td class="p-3 ${devColor}">${e.payload.device}</td>
                                <td class="p-3 text-teal-400 font-semibold">${e.payload.latency_ms.toFixed(3)} ms</td>
                            </tr>
                        `;
                    }).join('');

                    // Populate select options
                    attns.forEach(ev => {
                        const name = ev.payload.layer_name;
                        attentionCache[name] = ev.payload;
                        if (!loadedLayers.has(name)) {
                            loadedLayers.add(name);
                            const select = document.getElementById('attn-layer');
                            if (select.children[0] && select.children[0].innerText === "No layers loaded") {
                                select.innerHTML = "";
                            }
                            const opt = document.createElement('option');
                            opt.value = name;
                            opt.innerText = name;
                            select.appendChild(opt);
                        }
                    });

                    // Populate semantic layer options
                    traces.forEach(ev => {
                        const name = ev.payload.layer_name;
                        if (ev.payload.semantic_neighbors && ev.payload.semantic_neighbors.length > 0) {
                            semanticCache[name] = ev.payload.semantic_neighbors;
                            if (!semanticLayersLoaded.has(name)) {
                                semanticLayersLoaded.add(name);
                                const semSelect = document.getElementById('semantic-layer');
                                if (semSelect.children[0] && semSelect.children[0].innerText === "No layers loaded") {
                                    semSelect.innerHTML = "";
                                }
                                const opt = document.createElement('option');
                                opt.value = name;
                                opt.innerText = name;
                                semSelect.appendChild(opt);
                            }
                        }
                    });

                    // Auto-update semantic tokens if loaded for the first time
                    const semSelect = document.getElementById('semantic-layer');
                    if (semSelect.selectedIndex >= 0) {
                        const semTokenSelect = document.getElementById('semantic-token');
                        if (semTokenSelect.children[0] && semTokenSelect.children[0].innerText === "No tokens available") {
                            updateSemanticTokens();
                        }
                    }

                    updateDynamicHeadDropdown();
                    drawAttention();
                }).catch(e => console.error(e));

            // Query Replay Status
            fetch('/api/replay/status')
                .then(res => res.json())
                .then(rep => {
                    const replayPanel = document.getElementById('replay-panel');
                    if (rep.total_events > 0) {
                        document.getElementById('replay-status-text').innerText = rep.playing ? "Playing" : "Paused";
                        document.getElementById('live-feed-text').innerText = rep.playing ? "REPLAY ACTIVE" : "REPLAY PAUSED";
                        if (!rep.playing) {
                            document.getElementById('status-badge').querySelector('span').classList.remove('animate-ping');
                        } else {
                            document.getElementById('status-badge').querySelector('span').classList.add('animate-ping');
                        }
                        
                        const scrubber = document.getElementById('replay-scrubber');
                        scrubber.max = rep.total_events - 1;
                        scrubber.value = rep.current_index;
                        
                        document.getElementById('replay-index-start').innerText = rep.current_index;
                        document.getElementById('replay-index-end').innerText = rep.total_events;
                        
                        // Render visual scrubber [=====|----------]
                        document.getElementById('replay-visual-scrubber').innerText = 
                            updateVisualScrubber(rep.current_index, rep.total_events);

                        // Enable buttons & scrubber
                        document.getElementById('btn-replay-prev').removeAttribute('disabled');
                        document.getElementById('btn-replay-play').removeAttribute('disabled');
                        document.getElementById('btn-replay-pause').removeAttribute('disabled');
                        document.getElementById('btn-replay-next').removeAttribute('disabled');
                        document.getElementById('replay-scrubber').removeAttribute('disabled');
                        document.getElementById('btn-replay-live').removeAttribute('disabled');

                        // Toggle badge class & text
                        const badge = document.getElementById('replay-badge');
                        if (rep.replay_mode) {
                            badge.innerText = "Replay Mode";
                            badge.className = "text-[10px] px-2 py-0.5 rounded-full border border-yellow-500/30 text-yellow-400 bg-yellow-500/10 font-bold uppercase tracking-wider";
                        } else {
                            badge.innerText = "Live Mode";
                            badge.className = "text-[10px] px-2 py-0.5 rounded-full border border-slate-700 text-slate-400 bg-slate-800/50 font-bold uppercase tracking-wider";
                            document.getElementById('live-feed-text').innerText = "LIVE FEED";
                            document.getElementById('status-badge').querySelector('span').classList.add('animate-ping');
                        }
                    } else {
                        document.getElementById('btn-replay-prev').setAttribute('disabled', 'true');
                        document.getElementById('btn-replay-play').setAttribute('disabled', 'true');
                        document.getElementById('btn-replay-pause').setAttribute('disabled', 'true');
                        document.getElementById('btn-replay-next').setAttribute('disabled', 'true');
                        document.getElementById('replay-scrubber').setAttribute('disabled', 'true');
                        document.getElementById('btn-replay-live').setAttribute('disabled', 'true');
                        document.getElementById('live-feed-text').innerText = "LIVE FEED";
                    }
                }).catch(e => console.error(e));
        }

        function updateVisualScrubber(current, total) {
            if (total === 0) return "[ ]";
            const width = 15;
            const pos = Math.round((current / (total - 1)) * width);
            let bar = "";
            for (let i = 0; i < width; i++) {
                if (i === pos) bar += "|";
                else if (i < pos) bar += "=";
                else bar += "-";
            }
            return "[" + bar + "]";
        }

        function renderTopologyTree(modelName, numLayers) {
            const treeEl = document.getElementById('topology-tree');
            treeEl.innerHTML = "";

            // Root node
            const root = document.createElement('div');
            root.className = "font-bold text-teal-400 mb-1";
            root.innerText = `📁 ${modelName}`;
            treeEl.appendChild(root);

            // Embeddings node
            const embed = document.createElement('div');
            embed.className = "pl-4 text-slate-300 cursor-pointer hover:text-teal-300 py-0.5";
            embed.innerText = "├── 📄 embed_tokens (Embedding)";
            embed.onclick = () => selectTopologyLayer("embed_tokens");
            treeEl.appendChild(embed);

            // Layers container node
            const layersHeader = document.createElement('div');
            layersHeader.className = "pl-4 font-semibold text-emerald-400 cursor-pointer hover:text-emerald-300 py-0.5";
            layersHeader.innerText = `${layersExpanded ? '▼' : '▶'} 📁 layers (${numLayers} blocks)`;
            layersHeader.onclick = () => {
                layersExpanded = !layersExpanded;
                renderTopologyTree(modelName, numLayers);
            };
            treeEl.appendChild(layersHeader);

            if (layersExpanded) {
                for (let i = 0; i < numLayers; ++i) {
                    const block = document.createElement('div');
                    block.className = "pl-8 text-yellow-400/80 font-medium py-0.5 cursor-pointer hover:text-yellow-300";
                    block.innerText = `├─ 📁 layers.${i}`;
                    block.onclick = () => selectTopologyLayer(`layers.${i}.self_attn`);
                    treeEl.appendChild(block);

                    const subcomponents = [
                        { name: `layers.${i}.input_layernorm`, type: "RMSNorm" },
                        { name: `layers.${i}.self_attn`, type: "SelfAttention" },
                        { name: `layers.${i}.post_attention_layernorm`, type: "RMSNorm" },
                        { name: `layers.${i}.mlp`, type: "MLP" }
                    ];

                    subcomponents.forEach((sub, idx) => {
                        const subEl = document.createElement('div');
                        const prefix = (idx === 3) ? "│  └─ " : "│  ├─ ";
                        subEl.className = "pl-12 text-slate-400 hover:text-white cursor-pointer py-0.5";
                        subEl.innerText = `${prefix}📄 ${sub.name.split('.').pop()} (${sub.type})`;
                        subEl.onclick = (e) => {
                            e.stopPropagation();
                            selectTopologyLayer(sub.name);
                        };
                        treeEl.appendChild(subEl);
                    });
                }
            }

            // Norm node
            const norm = document.createElement('div');
            norm.className = "pl-4 text-slate-300 cursor-pointer hover:text-teal-300 py-0.5";
            norm.innerText = "├── 📄 norm (RMSNorm)";
            norm.onclick = () => selectTopologyLayer("norm");
            treeEl.appendChild(norm);

            // LM Head node
            const lmHead = document.createElement('div');
            lmHead.className = "pl-4 text-slate-300 cursor-pointer hover:text-teal-300 py-0.5";
            lmHead.innerText = "└── 📄 lm_head (LMHead)";
            lmHead.onclick = () => selectTopologyLayer("lm_head");
            treeEl.appendChild(lmHead);
        }

        function selectTopologyLayer(layerName) {
            const select = document.getElementById('attn-layer');
            let found = false;
            for (let i = 0; i < select.options.length; ++i) {
                if (select.options[i].value === layerName || layerName.includes(select.options[i].value)) {
                    select.selectedIndex = i;
                    found = true;
                    break;
                }
            }
            if (found) {
                updateDynamicHeadDropdown();
                drawAttention();
            }
        }

        function updateDynamicHeadDropdown() {
            const layerName = document.getElementById('attn-layer').value;
            const attnData = attentionCache[layerName];
            if (!attnData || !attnData.matrices) return;

            const selectHead = document.getElementById('attn-head');
            const currentVal = selectHead.value;
            
            // Check if count of options changed
            if (selectHead.options.length !== attnData.num_heads) {
                selectHead.innerHTML = "";
                for (let h = 0; h < attnData.num_heads; ++h) {
                    const opt = document.createElement('option');
                    opt.value = h;
                    opt.innerText = `Head ${h}`;
                    selectHead.appendChild(opt);
                }
                
                if (parseInt(currentVal) < attnData.num_heads) {
                    selectHead.value = currentVal;
                } else {
                    selectHead.value = 0;
                }
            }
        }

        function drawAttention() {
            const layerName = document.getElementById('attn-layer').value;
            const selectHead = document.getElementById('attn-head');
            if (selectHead.options.length === 0) return;
            const headIdx = parseInt(selectHead.value);
            
            const attnData = attentionCache[layerName];
            if (!attnData || !attnData.matrices || !attnData.matrices[headIdx]) return;

            const matrix = attnData.matrices[headIdx];
            const size = attnData.token_count;
            
            const canvas = document.getElementById('attnCanvas');
            const ctx = canvas.getContext('2d');
            const width = canvas.width;
            const height = canvas.height;
            
            ctx.clearRect(0, 0, width, height);

            const cellSize = width / size;
            
            for (let r = 0; r < size; ++r) {
                for (let c = 0; c < size; ++c) {
                    const weight = (matrix[r] && matrix[r][c] !== undefined) ? matrix[r][c] : 0;
                    ctx.fillStyle = `rgba(20, 184, 166, ${weight})`; // teal shade
                    ctx.fillRect(c * cellSize, r * cellSize, cellSize, cellSize);
                }
            }

            // Simple Shannon Entropy estimate
            let entropy = 0;
            for (let r = 0; r < size; ++r) {
                for (let c = 0; c < size; ++c) {
                    const w = (matrix[r] && matrix[r][c] !== undefined) ? matrix[r][c] : 0;
                    if (w > 1e-6) {
                        entropy -= w * Math.log2(w);
                    }
                }
            }
            document.getElementById('attn-entropy').innerText = (entropy / size).toFixed(3);
        }

        // Replay Event listeners
        document.getElementById('btn-replay-play').addEventListener('click', () => {
            fetch('/api/replay/play');
        });
        document.getElementById('btn-replay-pause').addEventListener('click', () => {
            fetch('/api/replay/pause');
        });
        document.getElementById('btn-replay-prev').addEventListener('click', () => {
            fetch('/api/replay/step_backward');
        });
        document.getElementById('btn-replay-next').addEventListener('click', () => {
            fetch('/api/replay/step_forward');
        });
        document.getElementById('btn-replay-live').addEventListener('click', () => {
            fetch('/api/replay/live');
        });
        document.getElementById('replay-scrubber').addEventListener('input', (e) => {
            fetch(`/api/replay/scrub?index=${e.target.value}`);
        });

        document.getElementById('attn-layer').addEventListener('change', () => {
            updateDynamicHeadDropdown();
            drawAttention();
        });
        document.getElementById('attn-head').addEventListener('change', drawAttention);

        // Summary calculations for comparison
        let summaryA = null;
        let summaryB = null;

        function getSummaryFromJSON(data) {
            if (data.stats && data.events) {
                let peakMem = "-";
                if (data.stats.gpu_available) {
                    peakMem = `${data.stats.vram_used_gb.toFixed(1)} GB (VRAM)`;
                } else {
                    peakMem = `${data.stats.ram_used_gb.toFixed(1)} GB (RAM)`;
                }
                return {
                    model: data.stats.slowest_layers && data.stats.slowest_layers.length > 0 ? data.stats.slowest_layers[0].name : (data.events.find(e => e.event_type === 'model_info')?.payload?.name || "Unknown"),
                    latency: data.stats.avg_inference_latency,
                    tps: data.stats.tokens_per_sec,
                    memory: peakMem,
                    anomalies_count: data.events.filter(e => e.event_type === 'anomaly').length
                };
            } else if (Array.isArray(data)) {
                const traces = data.filter(e => e.event_type === 'layer_trace');
                const modelInfo = data.find(e => e.event_type === 'model_info');
                const anomalies = data.filter(e => e.event_type === 'anomaly');
                
                let avgLat = 0;
                if (traces.length > 0) {
                    const sum = traces.reduce((acc, curr) => acc + curr.payload.latency_ms, 0);
                    avgLat = sum / traces.length;
                }
                
                const embedTraces = traces.filter(t => t.payload.layer_name.includes("embed") || t.payload.layer_type === "Embedding");
                let tps = 0;
                if (data.length > 1) {
                    const duration = (data[data.length - 1].timestamp - data[0].timestamp) / 1000.0;
                    if (duration > 0) {
                        tps = embedTraces.length / duration;
                    }
                }
                
                return {
                    model: modelInfo?.payload?.name || "Unknown",
                    latency: avgLat,
                    tps: tps,
                    memory: "N/A (Raw)",
                    anomalies_count: anomalies.length
                };
            }
            return null;
        }

        function updateComparisonTable() {
            const tbody = document.getElementById('comparison-table-body');
            if (!summaryA && !summaryB) {
                tbody.innerHTML = '<tr><td colspan="4" class="text-slate-500 text-center py-8">Please upload session JSON files or click \'Use Current Run\' to analyze comparison.</td></tr>';
                return;
            }
            
            const getVal = (summary, key) => (summary ? summary[key] : null);
            
            const renderRow = (metricName, key, unit = "", isLowerBetter = true) => {
                const valA = getVal(summaryA, key);
                const valB = getVal(summaryB, key);
                
                let displayA = "-";
                let displayB = "-";
                let deltaStr = "-";
                let deltaClass = "text-slate-400";
                
                if (valA !== null && valA !== undefined) {
                    displayA = typeof valA === 'number' ? valA.toFixed(2) + unit : valA;
                }
                if (valB !== null && valB !== undefined) {
                    displayB = typeof valB === 'number' ? valB.toFixed(2) + unit : valB;
                }
                
                if (typeof valA === 'number' && typeof valB === 'number') {
                    const diff = valB - valA;
                    const pct = valA !== 0 ? (diff / valA) * 100 : 0;
                    const sign = diff >= 0 ? "+" : "";
                    deltaStr = `${sign}${diff.toFixed(2)} (${sign}${pct.toFixed(1)}%)`;
                    
                    const isBetter = isLowerBetter ? (diff < 0) : (diff > 0);
                    if (Math.abs(diff) < 1e-5) {
                        deltaClass = "text-slate-400";
                    } else {
                        deltaClass = isBetter ? "text-emerald-400 font-bold" : "text-rose-400 font-bold";
                    }
                }
                
                return `
                    <tr class="border-b border-slate-800 hover:bg-slate-900/40 transition">
                        <td class="p-3 font-semibold text-slate-350">${metricName}</td>
                        <td class="p-3 text-slate-200">${displayA}</td>
                        <td class="p-3 text-slate-200">${displayB}</td>
                        <td class="p-3 ${deltaClass}">${deltaStr}</td>
                    </tr>
                `;
            };
            
            tbody.innerHTML = `
                <tr class="border-b border-slate-800 bg-slate-900/50">
                    <td class="p-3 font-bold text-slate-350">Model Name</td>
                    <td class="p-3 text-emerald-400 font-bold">${summaryA ? summaryA.model : "-"}</td>
                    <td class="p-3 text-emerald-400 font-bold">${summaryB ? summaryB.model : "-"}</td>
                    <td class="p-3 text-slate-400">-</td>
                </tr>
                ${renderRow("Average Latency", "latency", " ms", true)}
                ${renderRow("Throughput", "tps", " tok/s", false)}
                ${renderRow("Numerical Anomalies", "anomalies_count", "", true)}
                <tr class="border-b border-slate-800 hover:bg-slate-900/40 transition">
                    <td class="p-3 font-semibold text-slate-350">Peak Memory</td>
                    <td class="p-3 text-slate-200">${summaryA ? summaryA.memory : "-"}</td>
                    <td class="p-3 text-slate-200">${summaryB ? summaryB.memory : "-"}</td>
                    <td class="p-3 text-slate-400">-</td>
                </tr>
            `;
        }

        // Export Actions
        function exportSessionJSON() {
            Promise.all([
                fetch('/api/stats').then(res => res.json()),
                fetch('/api/events').then(res => res.json())
            ]).then(([stats, events]) => {
                const exportData = {
                    exporter: "LLMScope",
                    timestamp: new Date().toISOString(),
                    stats: stats,
                    events: events
                };
                const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `llmscope_session_${Date.now()}.json`;
                a.click();
                URL.revokeObjectURL(url);
            }).catch(err => console.error("Export JSON failed:", err));
        }

        function exportSessionCSV() {
            fetch('/api/events')
                .then(res => res.json())
                .then(events => {
                    const traces = events.filter(e => e.event_type === 'layer_trace');
                    let csv = "Event ID,Timestamp,Layer Name,Layer Type,Device,Latency (ms),Mean,Variance,Min,Max,Sparsity (%)\n";
                    traces.forEach(e => {
                        const p = e.payload;
                        const stats = p.stats || {};
                        csv += `${p.event_id},${e.timestamp},"${p.layer_name}","${p.layer_type}","${p.device}",${p.latency_ms},${stats.mean || 0},${stats.variance || 0},${stats.min || 0},${stats.max || 0},${stats.sparsity || 0}\n`;
                    });
                    const blob = new Blob([csv], { type: 'text/csv' });
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = `llmscope_traces_${Date.now()}.csv`;
                    a.click();
                    URL.revokeObjectURL(url);
                }).catch(err => console.error("Export CSV failed:", err));
        }

        function saveSessionOnServer() {
            fetch('/api/session/save')
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        alert(`Session saved successfully on server as: ${data.filename}`);
                    } else {
                        alert("Failed to save session on server.");
                    }
                }).catch(err => console.error("Save session failed:", err));
        }

        // Comparison event listeners
        document.getElementById('compare-file-a').addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = (evt) => {
                try {
                    const data = JSON.parse(evt.target.result);
                    summaryA = getSummaryFromJSON(data);
                    updateComparisonTable();
                } catch (err) {
                    alert("Error parsing Run A file: " + err.message);
                }
            };
            reader.readAsText(file);
        });

        document.getElementById('compare-file-b').addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = (evt) => {
                try {
                    const data = JSON.parse(evt.target.result);
                    summaryB = getSummaryFromJSON(data);
                    updateComparisonTable();
                } catch (err) {
                    alert("Error parsing Run B file: " + err.message);
                }
            };
            reader.readAsText(file);
        });

        document.getElementById('btn-use-current-a').addEventListener('click', () => {
            let latencyText = document.getElementById('stat-latency').innerText;
            let tpsText = document.getElementById('stat-tps').innerText;
            let anomaliesText = document.getElementById('stat-anomalies').innerText;
            
            summaryA = {
                model: document.getElementById('summary-model').innerText,
                latency: parseFloat(latencyText),
                tps: parseFloat(tpsText),
                memory: document.getElementById('summary-memory').innerText,
                anomalies_count: parseInt(anomaliesText)
            };
            updateComparisonTable();
        });

        document.getElementById('btn-compare-clear').addEventListener('click', () => {
            summaryA = null;
            summaryB = null;
            document.getElementById('compare-file-a').value = "";
            document.getElementById('compare-file-b').value = "";
            updateComparisonTable();
        });

        function updateSemanticTokens() {
            const layerName = document.getElementById('semantic-layer').value;
            const tokenSelect = document.getElementById('semantic-token');
            const cached = semanticCache[layerName];
            
            const currentVal = tokenSelect.value;
            tokenSelect.innerHTML = "";
            
            if (!cached || cached.length === 0) {
                const opt = document.createElement('option');
                opt.innerText = "No tokens available";
                tokenSelect.appendChild(opt);
                document.getElementById('semantic-neighbors-list').innerHTML = 
                    '<div class="text-slate-500 text-center py-16 text-xs">No semantic neighbor data found for this layer.</div>';
                return;
            }
            
            cached.forEach(sn => {
                const opt = document.createElement('option');
                opt.value = sn.token_index;
                opt.innerText = `[${sn.token_index}] "${sn.token_text}"`;
                tokenSelect.appendChild(opt);
            });
            
            if (currentVal && parseInt(currentVal) < cached.length) {
                tokenSelect.value = currentVal;
            } else {
                tokenSelect.value = 0;
            }
            
            renderSemanticNeighbors();
        }

        function renderSemanticNeighbors() {
            const layerName = document.getElementById('semantic-layer').value;
            const tokenIdx = parseInt(document.getElementById('semantic-token').value);
            const cached = semanticCache[layerName];
            const listEl = document.getElementById('semantic-neighbors-list');
            
            if (!cached || isNaN(tokenIdx) || !cached[tokenIdx]) {
                listEl.innerHTML = '<div class="text-slate-500 text-center py-16 text-xs">Select a layer and token to inspect semantic neighbors.</div>';
                return;
            }
            
            const sn = cached[tokenIdx];
            if (!sn.top_k || sn.top_k.length === 0) {
                listEl.innerHTML = '<div class="text-slate-500 text-center py-16 text-xs">No top-K items returned for this token.</div>';
                return;
            }
            
            listEl.innerHTML = sn.top_k.map((item, idx) => {
                const score = item.score;
                const barLength = 10;
                const filled = Math.round(Math.max(0, Math.min(1.0, score)) * barLength);
                const empty = barLength - filled;
                const barStr = "█".repeat(filled) + "░".repeat(empty);
                
                return `
                    <div class="flex items-center justify-between p-2.5 rounded-lg bg-slate-900/40 border border-slate-800 text-xs hover:bg-slate-900/70 transition">
                        <div class="flex items-center space-x-3 w-1/3">
                            <span class="text-slate-500 font-semibold mono font-mono">#${idx+1}</span>
                            <span class="text-emerald-400 font-bold font-mono break-all">${item.token}</span>
                        </div>
                        <div class="flex-1 flex items-center space-x-3">
                            <div class="text-slate-400 font-mono text-[10px] hidden sm:inline">${barStr}</div>
                            <div class="w-full bg-slate-950 border border-slate-850 rounded-full h-2 overflow-hidden flex">
                                <div class="bg-gradient-to-r from-teal-500 to-emerald-400 h-full rounded-full" style="width: ${Math.max(0, Math.min(100, score * 100))}%"></div>
                            </div>
                        </div>
                        <div class="w-20 text-right font-bold text-teal-400 font-mono pl-3">
                            ${score.toFixed(4)}
                        </div>
                    </div>
                `;
            }).join('');
        }

        // Semantic UI Event Listeners
        document.getElementById('semantic-layer').addEventListener('change', updateSemanticTokens);
        document.getElementById('semantic-token').addEventListener('change', renderSemanticNeighbors);

        // Arrow keys layer navigation
        document.addEventListener('keydown', (e) => {
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') {
                return;
            }
            
            const select = document.getElementById('semantic-layer');
            if (!select || select.options.length <= 1 || select.children[0]?.innerText === "No layers loaded") {
                return;
            }
            
            if (e.key === 'ArrowLeft') {
                e.preventDefault();
                let newIdx = select.selectedIndex - 1;
                if (newIdx < 0) newIdx = select.options.length - 1;
                select.selectedIndex = newIdx;
                updateSemanticTokens();
            } else if (e.key === 'ArrowRight') {
                e.preventDefault();
                let newIdx = select.selectedIndex + 1;
                if (newIdx >= select.options.length) newIdx = 0;
                select.selectedIndex = newIdx;
                updateSemanticTokens();
            }
        });

        // Export UI listeners
        document.getElementById('btn-export-json').addEventListener('click', exportSessionJSON);
        document.getElementById('btn-export-csv').addEventListener('click', exportSessionCSV);
        document.getElementById('btn-save-session').addEventListener('click', saveSessionOnServer);

        // Start updates
        updateStats();
        setInterval(updateStats, 800);
    </script>
</body>
</html>
)html";

WebServer::WebServer(EventBus& event_bus, 
                     RingBuffer& ring_buffer, 
                     TelemetryAggregator& aggregator, 
                     AnomalyDetector& anomaly_detector,
                     ReplayManager& replay_manager,
                     DeviceMonitor& device_monitor,
                     int port)
    : event_bus_(event_bus), 
      ring_buffer_(ring_buffer), 
      aggregator_(aggregator), 
      anomaly_detector_(anomaly_detector), 
      replay_manager_(replay_manager),
      device_monitor_(device_monitor),
      port_(port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (running_) return true;
    running_ = true;
    server_thread_ = std::thread(&WebServer::listen_loop, this);
    return true;
}

void WebServer::stop() {
    running_ = false;
    if (server_fd_ != -1) {
#ifdef _WIN32
        closesocket(static_cast<socket_t>(server_fd_));
#else
        close(static_cast<int>(server_fd_));
#endif
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void WebServer::listen_loop() {
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET_VAL) {
        running_ = false;
        return;
    }
    
    int opt = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    server_fd_ = s;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR_VAL) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        running_ = false;
        return;
    }

    if (listen(s, 10) == SOCKET_ERROR_VAL) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        running_ = false;
        return;
    }

    spdlog::info("Web server dashboard running at http://localhost:{}", port_);

    while (running_) {
        sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        socket_t client_sock = accept(s, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (client_sock == INVALID_SOCKET_VAL) {
            break;
        }
        
        handle_client(static_cast<int64_t>(client_sock));
    }
    
#ifdef _WIN32
    closesocket(s);
#endif
    running_ = false;
}

void WebServer::handle_client(int64_t client_socket) {
    socket_t sock = static_cast<socket_t>(client_socket);
    std::vector<char> buffer(4096);
    int bytes_read = recv(sock, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
    if (bytes_read <= 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return;
    }

    buffer[bytes_read] = '\0';
    std::string req(buffer.data());
    
    // Quick routing
    std::string response_content;
    std::string content_type = "text/html";
    std::string status = "200 OK";

    if (req.find("GET /api/events") != std::string::npos) {
        content_type = "application/json";
        std::vector<TelemetryEvent> all_ev = ring_buffer_.get_all();
        if (replay_manager_.is_replay_mode()) {
            size_t limit = replay_manager_.current_index();
            if (limit < all_ev.size()) {
                all_ev.resize(limit + 1);
            }
        }
        nlohmann::json j = all_ev;
        response_content = j.dump();
    } 
    else if (req.find("GET /api/stats") != std::string::npos) {
        content_type = "application/json";
        nlohmann::json j;
        j["tokens_per_sec"] = aggregator_.get_tokens_per_sec();
        j["avg_inference_latency"] = aggregator_.get_avg_inference_latency();
        j["events_processed"] = aggregator_.get_total_events_processed();
        
        auto devices = aggregator_.get_device_distribution();
        for (const auto& pair : devices) {
            j["devices"][pair.first] = pair.second;
        }

        // Add hardware stats
        SystemStats hw = device_monitor_.get_stats();
        j["gpu_available"] = hw.gpu_available;
        j["gpu_name"] = hw.gpu_name;
        j["gpu_utilization"] = hw.gpu_utilization;
        j["gpu_temp"] = hw.gpu_temp;
        j["cpu_usage"] = hw.cpu_usage;
        j["ram_used_gb"] = hw.ram_used_gb;
        j["ram_total_gb"] = hw.ram_total_gb;
        j["vram_used_gb"] = hw.vram_used_gb;
        j["vram_total_gb"] = hw.vram_total_gb;

        // Add slowest layers
        auto slowest = aggregator_.get_slowest_layers(10);
        nlohmann::json slow_arr = nlohmann::json::array();
        for (const auto& info : slowest) {
            nlohmann::json item;
            item["name"] = info.name;
            item["type"] = info.type;
            item["avg_latency_ms"] = info.avg_latency_ms;
            item["max_latency_ms"] = info.max_latency_ms;
            item["call_count"] = info.call_count;
            slow_arr.push_back(item);
        }
        j["slowest_layers"] = slow_arr;

        response_content = j.dump();
    }
    else if (req.find("GET /api/replay/status") != std::string::npos) {
        content_type = "application/json";
        nlohmann::json j;
        j["playing"] = replay_manager_.is_playing();
        j["current_index"] = replay_manager_.current_index();
        j["total_events"] = replay_manager_.total_events();
        j["replay_mode"] = replay_manager_.is_replay_mode();
        response_content = j.dump();
    }
    else if (req.find("GET /api/replay/play") != std::string::npos || req.find("POST /api/replay/play") != std::string::npos) {
        replay_manager_.set_replay_mode(true);
        replay_manager_.play();
        content_type = "application/json";
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/replay/pause") != std::string::npos || req.find("POST /api/replay/pause") != std::string::npos) {
        replay_manager_.set_replay_mode(true);
        replay_manager_.pause();
        content_type = "application/json";
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/replay/step_forward") != std::string::npos) {
        replay_manager_.set_replay_mode(true);
        replay_manager_.step_forward();
        content_type = "application/json";
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/replay/step_backward") != std::string::npos) {
        replay_manager_.set_replay_mode(true);
        replay_manager_.step_backward();
        content_type = "application/json";
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/replay/live") != std::string::npos) {
        replay_manager_.set_replay_mode(false);
        replay_manager_.pause();
        content_type = "application/json";
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/replay/scrub") != std::string::npos) {
        replay_manager_.set_replay_mode(true);
        content_type = "application/json";
        size_t idx = 0;
        size_t q_pos = req.find("index=");
        if (q_pos != std::string::npos) {
            std::string idx_str = req.substr(q_pos + 6);
            size_t end_pos = idx_str.find_first_not_of("0123456789");
            if (end_pos != std::string::npos) {
                idx_str = idx_str.substr(0, end_pos);
            }
            if (!idx_str.empty()) {
                idx = std::stoull(idx_str);
            }
        }
        replay_manager_.jump_to(idx);
        response_content = "{\"status\":\"ok\"}";
    }
    else if (req.find("GET /api/anomalies") != std::string::npos) {
        content_type = "application/json";
        if (replay_manager_.is_replay_mode()) {
            std::vector<TelemetryEvent> all_ev = ring_buffer_.get_all();
            size_t limit = replay_manager_.current_index();
            nlohmann::json j = nlohmann::json::array();
            for (size_t i = 0; i <= limit && i < all_ev.size(); ++i) {
                if (all_ev[i].event_type == "anomaly") {
                    j.push_back(all_ev[i].anomaly);
                }
            }
            response_content = j.dump();
        } else {
            nlohmann::json j = anomaly_detector_.get_alerts();
            response_content = j.dump();
        }
    }
    else if (req.find("GET /api/session/save") != std::string::npos || req.find("POST /api/session/save") != std::string::npos) {
        content_type = "application/json";
        std::vector<TelemetryEvent> all_ev = ring_buffer_.get_all();
        std::string filename = "session_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) + ".json";
        
        bool success = SessionStore::save(filename, all_ev);
        nlohmann::json j;
        j["success"] = success;
        j["filename"] = filename;
        response_content = j.dump();
    }
    else if (req.find("GET / ") != std::string::npos || req.find("GET /index.html") != std::string::npos) {
        response_content = DASHBOARD_HTML;
    }
    else {
        status = "404 Not Found";
        response_content = "404 Not Found";
    }

    std::stringstream ss;
    ss << "HTTP/1.1 " << status << "\r\n"
       << "Content-Type: " << content_type << "; charset=utf-8\r\n"
       << "Content-Length: " << response_content.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << response_content;

    std::string resp = ss.str();
    send(sock, resp.data(), static_cast<int>(resp.size()), 0);

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}
