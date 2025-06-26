# ChunkStream Library

ChunkStream is a high-performance C++ library for reliable UDP-based data streaming with automatic chunking and reassembly. It provides robust mechanisms for transmitting large data frames over UDP with built-in error detection, retransmission.

## Features

- **Large Data Frame Transmission**: Automatically chunks large data into UDP-sized packets and reassembles them at the receiver
- **Reliable UDP Communication**: Built-in packet loss detection and automatic retransmission requests
- **High Performance**: Optimized for low-latency, high-throughput data streaming
- **Memory Pool Management**: Efficient memory allocation with pre-allocated buffer pools
- **Thread Safety**: Multi-threaded design with thread pool for concurrent packet processing
- **Configurable Parameters**: Adjustable MTU size, buffer sizes, and timeout settings
- **Real-time Monitoring**: Built-in statistics tracking for performance analysis

## Core Concepts

- **Sender**: Transmits large data frames by splitting them into UDP packets with sequence information
- **Receiver**: Receives UDP packets, reassembles them into original data frames, and requests retransmission for missing packets
- **Chunks**: Individual UDP packets containing part of a larger data frame with header information
- **Memory Pools**: Pre-allocated memory buffers for efficient packet and frame management

## Requirements

- C++17 compiler
- CMake 3.10 or higher
- ASIO library (standalone or Boost.ASIO)

## Building the Library

### Windows (vcpkg)

1. Install prerequisites:
   - Visual Studio 2019 or later
   - CMake 3.10+
   ```powershell
   vcpkg install asio:x64-windows
   ```

2. Clone the repository:
   ```powershell
   git clone https://github.com/your-org/libchunkstream-cpp.git
   cd libchunkstream-cpp
   ```

3. Build the library:
   ```powershell
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
   cmake --build . --config Release
   cmake --build . --config Debug
   ```

### Linux

1. Install prerequisites:
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential cmake libasio-dev
   ```

2. Clone the repository:
   ```bash
   git clone https://github.com/your-org/libchunkstream-cpp.git
   cd libchunkstream-cpp
   ```

3. Build the library:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

4. Install the library (optional):
   ```bash
   sudo make install
   ```
   This will install the library and headers to your system directories, typically under `/usr/local/`.

## Basic Usage

### Sender Example

```cpp
#include "chunkstream/sender.h"
#include <vector>
#include <iostream>

int main() {
    // Create sender with target IP, port, and optional parameters
    chunkstream::Sender sender("192.168.1.100", 5555, 1500, 50, 10485760);
    
    // Start sender in background thread
    std::thread sender_thread([&sender]() {
        sender.Start();
    });
    
    // Prepare data to send
    std::vector<uint8_t> large_data(5000000);  // 5MB data
    // Fill with your data...
    
    // Send data frame
    sender.Send(large_data.data(), large_data.size());
    
    std::cout << "Data sent successfully" << std::endl;
    
    // Cleanup
    sender.Stop();
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    
    return 0;
}
```

### Receiver Example

```cpp
#include "chunkstream/receiver.h"
#include <iostream>
#include <vector>

int main() {
    // Data reception callback
    auto onDataReceived = [](const std::vector<uint8_t>& data, std::function<void()> release) {
        std::cout << "Received data frame of size: " << data.size() << " bytes" << std::endl;
        
        // Process your data here...
        
        // Important: Call release when done processing
        release();
    };
    
    // Create receiver with port and callback
    chunkstream::Receiver receiver(5555, onDataReceived, 1500, 50, 10485760);
    
    std::cout << "Starting receiver on port 5555..." << std::endl;
    
    // Start receiver (blocking call)
    receiver.Start();
    
    return 0;
}
```

### Advanced Configuration

```cpp
// Sender with custom parameters
chunkstream::Sender sender(
    "192.168.1.100",    // Target IP
    5555,               // Target port
    9000,               // MTU size (jumbo frames)
    100,                // Buffer size (number of concurrent frames)
    50000000            // Maximum data size per frame (50MB)
);

// Receiver with custom parameters
chunkstream::Receiver receiver(
    5555,               // Listen port
    callback,           // Data received callback
    9000,               // MTU size
    100,                // Buffer size
    50000000            // Maximum data size per frame
);
```

## Configuration Parameters

| Parameter | Description | Default | Recommended Range |
|-----------|-------------|---------|-------------------|
| **MTU** | Maximum Transmission Unit size | 1500 | 1500-9000 |
| **Buffer Size** | Number of concurrent frames in memory | 10 | 10-100 |
| **Max Data Size** | Maximum size per data frame | 0 (unlimited) | 1MB-100MB |
| **Port** | UDP port for communication | User-defined | 1024-65535 |

## Performance Tuning

### For Low Latency
```cpp
// Small MTU, frequent transmission
chunkstream::Sender sender(ip, port, 1500, 10, max_size);
```

### For High Throughput
```cpp
// Large MTU (if network supports), larger buffers
chunkstream::Sender sender(ip, port, 9000, 100, max_size);
```

### Memory Optimization
```cpp
// Smaller buffer sizes for memory-constrained environments
chunkstream::Receiver receiver(port, callback, 1500, 20, 5000000);
```

## Error Handling and Monitoring

```cpp
// Monitor transmission statistics
std::cout << "Frames received: " << receiver.GetFrameCount() << std::endl;
std::cout << "Frames dropped: " << receiver.GetDropCount() << std::endl;

// Flush pending frames (cleanup)
receiver.Flush();
```

## Testing and Data Integrity Verification

The library includes a comprehensive test application for data integrity verification and performance analysis.

### Test Application Usage

```bash
# Display help and usage information
./chunkstream_example --help

# Run integrated sender/receiver test with data verification (default)
./chunkstream_example both

# Run with custom port
./chunkstream_example both --port 8080

# Run sender only to specific host and port
./chunkstream_example sender --host 192.168.1.100 --port 5555

# Run receiver only on specific port
./chunkstream_example receiver --port 5555
```

### Command Line Options

| Option | Description | Available Modes | Default |
|--------|-------------|-----------------|---------|
| `--host HOST` | Target IP address | sender only | 127.0.0.1 |
| `--port PORT` | UDP port number | all modes | 56343 |
| `--help, -h` | Show help message | all modes | - |

### Test Modes

#### 1. Both Mode (Default)
```bash
# Local loopback test with data integrity verification
./chunkstream_example both --port 9090
```
- Runs both sender and receiver in same process
- Automatic data integrity verification
- Real-time performance statistics
- Ideal for library testing and benchmarking

#### 2. Sender Mode
```bash
# Send to remote receiver
./chunkstream_example sender --host 192.168.1.100 --port 5555
```
- Continuously sends test data frames
- Performance statistics display
- Useful for network testing and load generation

#### 3. Receiver Mode
```bash
# Receive on specific port
./chunkstream_example receiver --port 5555
```
- Listens for incoming data frames
- Data integrity verification
- Performance monitoring
- Ideal for testing receiver performance

### Test Application Features

The test application provides comprehensive analysis:

- **Real-time Statistics**: Live FPS, throughput, and performance metrics
- **Data Integrity Verification**: Automatic corruption detection and reporting
- **Packet Loss Analysis**: Drop rate monitoring and statistics
- **Latency Measurements**: Round-trip time and distribution analysis
- **Network Performance**: Throughput and efficiency metrics

### Example Test Session

```bash
# Terminal 1: Start receiver
./chunkstream_example receiver --port 8080

# Terminal 2: Start sender to remote host
./chunkstream_example sender --host 192.168.1.100 --port 8080

# Press Enter to stop test and view detailed results
```

### Network Testing Scenarios

```bash
# Local performance benchmark
./chunkstream_example both --port 7777

# Cross-network reliability test
./chunkstream_example sender --host 10.0.0.50 --port 6666

# High-port testing (avoiding conflicts)
./chunkstream_example both --port 55555
```

## Thread Safety

ChunkStream is designed for multi-threaded environments:

- **Thread Pool**: Automatic work distribution across available CPU cores
- **Memory Pools**: Thread-safe memory allocation and deallocation
- **Atomic Counters**: Lock-free statistics tracking
- **Mutex Protection**: Critical sections properly protected

## Network Considerations

### Firewall Settings
Ensure UDP traffic is allowed on your chosen ports:

```bash
# Linux (iptables)
sudo iptables -A INPUT -p udp --dport 5555 -j ACCEPT

# Windows (PowerShell as Administrator)
New-NetFirewallRule -DisplayName "ChunkStream" -Direction Inbound -Protocol UDP -LocalPort 5555 -Action Allow
```

### Network Performance
- **Jumbo Frames**: Use MTU 9000 for high-speed networks
- **Buffer Tuning**: Increase system UDP buffer sizes for high-throughput applications
- **CPU Affinity**: Consider pinning threads to specific CPU cores for consistent performance

## Troubleshooting

### Common Issues

1. **High Packet Loss**
   - Reduce MTU size
   - Increase buffer sizes
   - Check network capacity

2. **Memory Issues**
   - Reduce buffer_size parameter
   - Implement proper release() callback handling
   - Monitor memory usage with system tools

3. **Performance Issues**
   - Enable compiler optimizations (-O3)
   - Use Release build configuration
   - Consider network hardware limitations

4. **Connection Issues**
   - Verify firewall settings
   - Check port availability with `netstat -an | grep PORT`
   - Ensure correct IP addresses and routing

### Testing Tips

- Use `both` mode for initial testing and library validation
- Test with `sender`/`receiver` modes for network-specific scenarios
- Monitor system resources during high-throughput tests
- Use different ports to avoid conflicts with existing services

## License
This project is licensed under the MIT License - see the LICENSE file for details.