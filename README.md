# QoS-Aware Traffic Management System

## Project Overview

The QoS-Aware Traffic Management System is a simulation-based network traffic management project that compares a traditional First-In-First-Out (FIFO) queue with a Quality of Service (QoS) pipeline.

The system simulates voice, video, and bulk data traffic over a congested network link and evaluates how different queue management and scheduling strategies affect network performance.

## Features

- **FIFO Baseline:** Simulates a shared queue with tail-drop packet handling.
- **Traffic Policing:** Uses token-bucket policing for different traffic classes.
- **Per-Class Queues:** Maintains separate queues for voice, video, and bulk traffic.
- **QoS Scheduling:** Implements strict-priority scheduling for voice and Deficit Round Robin (DRR) scheduling for video and bulk traffic.
- **RED-Style Marking:** Includes a simplified RED-style marking mechanism for bulk traffic.
- **Performance Analysis:** Measures latency, 99th-percentile latency, jitter, packet loss, and delivered throughput.
- **Interactive Dashboard:** Provides congestion presets, adjustable simulation parameters, performance charts, and a packet pipeline animation.

## Technologies Used

- C++17
- Python 3
- HTML, CSS, and JavaScript
- Tailwind CSS
- Chart.js

## Project Structure

```text
QoS-Aware-Traffic-Management/
├── index.html       # Web dashboard
├── qos_engine.cpp   # C++ simulation engine
├── server.py        # Python HTTP server and API
└── README.md        # Project documentation
```

## How to Run

### Prerequisites

- Python 3
- A C++ compiler supporting C++17, such as g++

### Steps

1. Clone or download this repository.
2. Open a terminal in the project directory.
3. Run the Python server:

   ```bash
   python3 server.py
   ```

4. Open the following URL in your browser:

   `http://localhost:8000`

The server automatically compiles the C++ simulation engine when required.

## Performance Metrics

The simulation compares FIFO and QoS using:

- Average latency
- 99th-percentile latency
- Jitter
- Packet loss
- Delivered throughput
- Policer drops, queue drops, and ECN marks

## Limitations

This project simulates a single bottleneck link. It does not operate on physical network equipment or model multi-hop routing or closed-loop TCP congestion control. RED-style marking and ECN behaviour are simplified, and their effects depend on the simulation parameters.

## Team Members

- Pratik Mohanty — 24BKT0149
- Raghavan E M — 24BCI0219
- Inba Senthil Kumar — 24BKT0108

## Course

Computer Networks (BCSE308L)  
School of Computer Science and Engineering (SCOPE)  
Vellore Institute of Technology, Vellore
