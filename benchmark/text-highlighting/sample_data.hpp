#pragma once
#include <string_view>

namespace ally::benchmark {

constexpr std::string_view kSmallMarkdown = R"md(
# Quick Sort Implementation

A simple quicksort in C++. This version uses the Lomuto partition scheme.

```cpp
#include <vector>
#include <algorithm>

template <typename T>
int partition(std::vector<T>& arr, int low, int high) {
    T pivot = arr[high];
    int i = low - 1;
    for (int j = low; j < high; ++j) {
        if (arr[j] <= pivot) {
            ++i;
            std::swap(arr[i], arr[j]);
        }
    }
    std::swap(arr[i + 1], arr[high]);
    return i + 1;
}

template <typename T>
void quicksort(std::vector<T>& arr, int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);
        quicksort(arr, low, pi - 1);
        quicksort(arr, pi + 1, high);
    }
}
```
)md";

constexpr std::string_view kMediumMarkdown = R"md(
# Data Processing Pipeline

This module handles the **ETL pipeline** for incoming sensor data.

## Key Features

- Batch ingestion from S3
- *Streaming* mode via Kafka
- Automatic schema detection
- Configurable `retry_policy` for transient failures

> **Note:** The pipeline must maintain ordering guarantees within each
> partition. Out-of-order events are buffered for up to 30 seconds.

## Python Processor

```python
import asyncio
from dataclasses import dataclass, field
from typing import Optional, List

@dataclass
class SensorReading:
    sensor_id: str
    timestamp: float
    value: float
    metadata: dict = field(default_factory=dict)

class BatchProcessor:
    def __init__(self, batch_size: int = 100, timeout: float = 30.0):
        self.batch_size = batch_size
        self.timeout = timeout
        self._buffer: List[SensorReading] = []
        self._lock = asyncio.Lock()

    async def ingest(self, reading: SensorReading) -> Optional[List[SensorReading]]:
        async with self._lock:
            self._buffer.append(reading)
            if len(self._buffer) >= self.batch_size:
                batch = self._buffer[:]
                self._buffer.clear()
                return batch
        return None

    async def flush(self) -> List[SensorReading]:
        async with self._lock:
            batch = self._buffer[:]
            self._buffer.clear()
            return batch
```

## JavaScript Consumer

```javascript
const { Kafka } = require('kafkajs');

class EventConsumer {
  constructor(brokers, groupId, topic) {
    this.kafka = new Kafka({ brokers });
    this.consumer = this.kafka.consumer({ groupId });
    this.topic = topic;
    this.handlers = new Map();
  }

  on(eventType, handler) {
    this.handlers.set(eventType, handler);
    return this;
  }

  async start() {
    await this.consumer.connect();
    await this.consumer.subscribe({ topic: this.topic, fromBeginning: false });

    await this.consumer.run({
      eachMessage: async ({ topic, partition, message }) => {
        const event = JSON.parse(message.value.toString());
        const handler = this.handlers.get(event.type);
        if (handler) {
          await handler(event.payload, { topic, partition, offset: message.offset });
        }
      },
    });
  }

  async stop() {
    await this.consumer.disconnect();
  }
}

module.exports = { EventConsumer };
```
)md";

constexpr std::string_view kLargeMarkdown = R"md(
# Distributed Task Scheduler

A **distributed task scheduler** built for high-throughput workloads. It supports
*priority queues*, task dependencies, and automatic retries with exponential backoff.

## Architecture Overview

The scheduler consists of three main components:

1. **Coordinator** — assigns tasks to workers based on capacity and affinity
2. **Worker Pool** — executes tasks and reports results back
3. **State Store** — persists task state for crash recovery

### Design Constraints

- Tasks must be **idempotent** — the scheduler may retry on timeout
- Maximum task execution time: `300s` (configurable per queue)
- Workers report heartbeats every `10s`; missed heartbeats trigger reassignment

> The coordinator uses a **consistent hashing ring** to distribute tasks.
> When a worker joins or leaves, only `1/N` of tasks are redistributed.
> This minimizes disruption during rolling deployments.

## Core Implementation (C++)

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <string>

enum class TaskState { Pending, Running, Completed, Failed, Retrying };

struct Task {
    std::string id;
    int priority;
    std::function<void()> work;
    TaskState state = TaskState::Pending;
    int retry_count = 0;
    int max_retries = 3;
    std::chrono::steady_clock::time_point deadline;

    bool operator<(const Task& other) const {
        return priority < other.priority;  // max-heap
    }
};

class TaskQueue {
  public:
    void push(Task task) {
        std::lock_guard lock(mutex_);
        task.state = TaskState::Pending;
        queue_.push(std::move(task));
        cv_.notify_one();
    }

    std::optional<Task> pop(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [&] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        auto task = std::move(const_cast<Task&>(queue_.top()));
        queue_.pop();
        return task;
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Task> queue_;
};
```

## Worker Implementation (Rust)

```rust
use std::sync::Arc;
use tokio::sync::{mpsc, Mutex};
use tokio::time::{self, Duration};
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TaskResult {
    pub task_id: String,
    pub success: bool,
    pub output: Option<String>,
    pub duration_ms: u64,
}

pub struct Worker {
    id: String,
    heartbeat_interval: Duration,
    result_tx: mpsc::Sender<TaskResult>,
    active_tasks: Arc<Mutex<Vec<String>>>,
}

impl Worker {
    pub fn new(id: String, result_tx: mpsc::Sender<TaskResult>) -> Self {
        Self {
            id,
            heartbeat_interval: Duration::from_secs(10),
            result_tx,
            active_tasks: Arc::new(Mutex::new(Vec::new())),
        }
    }

    pub async fn execute(&self, task_id: &str, work: impl FnOnce() -> Result<String, String>) {
        {
            let mut tasks = self.active_tasks.lock().await;
            tasks.push(task_id.to_string());
        }

        let start = std::time::Instant::now();
        let result = match work() {
            Ok(output) => TaskResult {
                task_id: task_id.to_string(),
                success: true,
                output: Some(output),
                duration_ms: start.elapsed().as_millis() as u64,
            },
            Err(_) => TaskResult {
                task_id: task_id.to_string(),
                success: false,
                output: None,
                duration_ms: start.elapsed().as_millis() as u64,
            },
        };

        let _ = self.result_tx.send(result).await;

        {
            let mut tasks = self.active_tasks.lock().await;
            tasks.retain(|t| t != task_id);
        }
    }

    pub async fn heartbeat_loop(&self, coordinator_url: &str) {
        let mut interval = time::interval(self.heartbeat_interval);
        loop {
            interval.tick().await;
            let tasks = self.active_tasks.lock().await;
            // Send heartbeat with active task list
            println!("Worker {} heartbeat: {} active tasks", self.id, tasks.len());
        }
    }
}
```

## Python Monitoring

```python
import time
import statistics
from collections import defaultdict
from typing import Dict, List, NamedTuple

class TaskMetrics(NamedTuple):
    task_id: str
    duration_ms: float
    success: bool
    retries: int

class MetricsCollector:
    def __init__(self):
        self._metrics: Dict[str, List[TaskMetrics]] = defaultdict(list)
        self._start_time = time.monotonic()

    def record(self, queue: str, metric: TaskMetrics):
        self._metrics[queue].append(metric)

    def summary(self, queue: str) -> dict:
        entries = self._metrics.get(queue, [])
        if not entries:
            return {"count": 0}

        durations = [m.duration_ms for m in entries]
        successes = sum(1 for m in entries if m.success)
        retries = sum(m.retries for m in entries)

        return {
            "count": len(entries),
            "success_rate": successes / len(entries),
            "total_retries": retries,
            "p50_ms": statistics.median(durations),
            "p95_ms": sorted(durations)[int(len(durations) * 0.95)],
            "p99_ms": sorted(durations)[int(len(durations) * 0.99)],
            "mean_ms": statistics.mean(durations),
        }

    def uptime_seconds(self) -> float:
        return time.monotonic() - self._start_time
```

## Configuration (JavaScript)

```javascript
const DEFAULT_CONFIG = {
  coordinator: {
    port: 8080,
    hashRingReplicas: 150,
    rebalanceInterval: 60000,
  },
  worker: {
    heartbeatInterval: 10000,
    maxConcurrentTasks: 4,
    taskTimeout: 300000,
  },
  retry: {
    maxRetries: 3,
    baseDelay: 1000,
    maxDelay: 30000,
    backoffMultiplier: 2.0,
  },
};

function mergeConfig(base, overrides) {
  const result = { ...base };
  for (const [key, value] of Object.entries(overrides)) {
    if (typeof value === 'object' && value !== null && !Array.isArray(value)) {
      result[key] = mergeConfig(base[key] || {}, value);
    } else {
      result[key] = value;
    }
  }
  return result;
}

function validateConfig(config) {
  const errors = [];
  if (config.worker.heartbeatInterval >= config.worker.taskTimeout) {
    errors.push('heartbeatInterval must be less than taskTimeout');
  }
  if (config.retry.baseDelay > config.retry.maxDelay) {
    errors.push('baseDelay must not exceed maxDelay');
  }
  if (config.coordinator.hashRingReplicas < 1) {
    errors.push('hashRingReplicas must be at least 1');
  }
  return errors;
}

module.exports = { DEFAULT_CONFIG, mergeConfig, validateConfig };
```

## Diff: Recent Priority Fix

```diff
--- a/src/scheduler/task_queue.cpp
+++ b/src/scheduler/task_queue.cpp
@@ -15,7 +15,9 @@ void TaskQueue::push(Task task) {
     task.state = TaskState::Pending;
-    queue_.push(std::move(task));
+    if (task.priority > 0) {
+        queue_.push(std::move(task));
+    }
     cv_.notify_one();
 }

@@ -28,6 +30,8 @@ std::optional<Task> TaskQueue::pop(std::chrono::milliseconds timeout) {
     auto task = std::move(const_cast<Task&>(queue_.top()));
     queue_.pop();
+    task.state = TaskState::Running;
+    task.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
     return task;
 }
```
)md";

}  // namespace ally::benchmark
