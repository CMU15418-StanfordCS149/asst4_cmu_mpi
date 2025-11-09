#include <algorithm>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>

#include <mpi.h>

#include "wireroute.h"

#include <random>
#include <climits>
#include <cassert>

#define ROOT 0

// 打印占用统计信息
void print_stats(const std::vector<std::vector<int>>& occupancy) {
  int max_occupancy = 0;
  long long total_cost = 0;

  for (const auto& row : occupancy) {
    for (const int count : row) {
      max_occupancy = std::max(max_occupancy, count);
      total_cost += count * count;
    }
  }

  std::cout << "Max occupancy: " << max_occupancy << '\n';
  std::cout << "Total cost: " << total_cost << '\n';
}

// 写入输出文件
void write_output(const std::vector<Wire>& wires, const int num_wires, const std::vector<std::vector<int>>& occupancy, const int dim_x, const int dim_y, const int nproc, std::string input_filename) {
  if (std::size(input_filename) >= 4 && input_filename.substr(std::size(input_filename) - 4) == ".txt") {
    input_filename.resize(std::size(input_filename) - 4);
  }

  const std::string occupancy_filename = input_filename + "_occupancy_" + std::to_string(nproc) + ".txt";
  const std::string wires_filename = input_filename + "_wires_" + std::to_string(nproc) + ".txt";

  std::ofstream out_occupancy(occupancy_filename, std::fstream::out);
  if (!out_occupancy) {
    std::cerr << "Unable to open file: " << occupancy_filename << '\n';
    exit(EXIT_FAILURE);
  }

  out_occupancy << dim_x << ' ' << dim_y << '\n';
  for (const auto& row : occupancy) {
    for (const int count : row) {
      out_occupancy << count << ' ';
    }
    out_occupancy << '\n';
  }

  out_occupancy.close();

  std::ofstream out_wires(wires_filename, std::fstream:: out);
  if (!out_wires) {
    std::cerr << "Unable to open file: " << wires_filename << '\n';
    exit(EXIT_FAILURE);
  }

  out_wires << dim_x << ' ' << dim_y << '\n' << num_wires << '\n';

  for (const auto& [start_x, start_y, end_x, end_y, bend1_x, bend1_y] : wires) {
    out_wires << start_x << ' ' << start_y << ' ' << bend1_x << ' ' << bend1_y << ' ';

    if (start_y == bend1_y) {
    // 第一个弯道是水平的

      if (end_x != bend1_x) {
        // 有两个弯道

        out_wires << bend1_x << ' ' << end_y << ' ';
      }
    } else if (start_x == bend1_x) {
      // 第一个弯道是垂直的

      if (end_y != bend1_y) {
        // 有两个弯道

        out_wires << end_x << ' ' << bend1_y << ' ';
      }
    }
    out_wires << end_x << ' ' << end_y << '\n';
  }

  out_wires.close();
}

void serial_cal_occupancy(std::vector<std::vector<int>>& occupancy, const std::vector<Wire>& wires) {
  // 先对 occupancy 矩阵置0
  for(auto &row : occupancy) {
    std::fill(row.begin(), row.end(), 0);
  }
  // 根据 wires 信息更新 occupancy 矩阵
  for(const Wire &wire : wires) {
    // Wire: int start_x, start_y, end_x, end_y, bend1_x, bend1_y;
    assert(wire.start_y == wire.bend1_y || wire.start_x == wire.bend1_x);
    if (wire.start_y == wire.bend1_y) {
    // 第一个弯道是水平的
      // 起始点到第一个弯折点 (横的)
      for(int x = wire.start_x; x != wire.bend1_x; x += (wire.start_x < wire.bend1_x ? 1 : -1)) {
        occupancy[wire.start_y][x]++;
      }
      if (wire.end_x != wire.bend1_x) {
        // 有两个弯道
        // 第一个弯折点到第二个弯折点 (竖的)
        for(int y = wire.bend1_y; y != wire.end_y; y += (wire.bend1_y < wire.end_y ? 1 : -1)) {
          occupancy[y][wire.bend1_x]++;
        }
        // 第二个弯折点到终点 (横的)
        for(int x = wire.bend1_x; x != wire.end_x; x += (wire.bend1_x < wire.end_x ? 1 : -1)) {
          occupancy[wire.end_y][x]++;
        }
      }
      else {
        // 垂直线段
        for(int y = wire.start_y; y != wire.end_y; y += (wire.start_y < wire.end_y ? 1 : -1)) {
          occupancy[y][wire.end_x]++;
        }
      }
    } else if (wire.start_x == wire.bend1_x) {
      // 第一个弯道是垂直的
      // 起始点到第一个弯折点 (竖的)
      for(int y = wire.start_y; y != wire.bend1_y; y += (wire.start_y < wire.bend1_y ? 1 : -1)) {
        occupancy[y][wire.start_x]++;
      }
      if (wire.end_y != wire.bend1_y) {
        // 有两个弯道
        // 第一个弯折点到第二个弯折点 (横的)
        for(int x = wire.bend1_x; x != wire.end_x; x += (wire.bend1_x < wire.end_x ? 1 : -1)) {
          occupancy[wire.bend1_y][x]++;
        }
        // 第二个弯折点到终点 (竖的)
        for(int y = wire.bend1_y; y != wire.end_y; y += (wire.bend1_y < wire.end_y ? 1 : -1)) {
          occupancy[y][wire.end_x]++;
        }
      }
      // 理论上来说，水平线已经在前面的分支处理掉了
      assert(wire.end_y != wire.bend1_y);
    }
    // 终点 occupancy +1
    occupancy[wire.end_y][wire.end_x]++;
  }
}

// 计算单条线的路径成本
// isAlready: 参数表示该路径是否已经被占用（即该路径是否已经被计算过并更新了占用矩阵）
long long compute_path_cost(const Wire& wire, const std::vector<std::vector<int>>& occupancy, bool isAlready) {
  long long cost = 0;

  assert(wire.start_y == wire.bend1_y || wire.start_x == wire.bend1_x);
  
  if (wire.start_y == wire.bend1_y) {
    // 第一个弯道是水平的
    // 起始点到第一个弯折点 (横的)
    for(int x = wire.start_x; x != wire.bend1_x; x += (wire.start_x < wire.bend1_x ? 1 : -1)) {
      int occ = occupancy[wire.start_y][x];
      cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
    }
    if (wire.end_x != wire.bend1_x) {
      // 有两个弯道
      // 第一个弯折点到第二个弯折点 (竖的)
      for(int y = wire.bend1_y; y != wire.end_y; y += (wire.bend1_y < wire.end_y ? 1 : -1)) {
        int occ = occupancy[y][wire.bend1_x];
        cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
      }
      // 第二个弯折点到终点 (横的)
      for(int x = wire.bend1_x; x != wire.end_x; x += (wire.bend1_x < wire.end_x ? 1 : -1)) {
        int occ = occupancy[wire.end_y][x];
        cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
      }
    }
    else {
      // 垂直线段
      for(int y = wire.start_y; y != wire.end_y; y += (wire.start_y < wire.end_y ? 1 : -1)) {
        int occ = occupancy[y][wire.end_x];
        cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
      }
    }
  } else if (wire.start_x == wire.bend1_x) {
    // 第一个弯道是垂直的
    // 起始点到第一个弯折点 (竖的)
    for(int y = wire.start_y; y != wire.bend1_y; y += (wire.start_y < wire.bend1_y ? 1 : -1)) {
      int occ = occupancy[y][wire.start_x];
      cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
    }
    if (wire.end_y != wire.bend1_y) {
      // 有两个弯道
      // 第一个弯折点到第二个弯折点 (横的)
      for(int x = wire.bend1_x; x != wire.end_x; x += (wire.bend1_x < wire.end_x ? 1 : -1)) {
        int occ = occupancy[wire.bend1_y][x];
        cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
      }
      // 第二个弯折点到终点 (竖的)
      for(int y = wire.bend1_y; y != wire.end_y; y += (wire.bend1_y < wire.end_y ? 1 : -1)) {
        int occ = occupancy[y][wire.end_x];
        cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
      }
    }
    // 理论上来说，水平线已经在前面的分支处理掉了
    assert(wire.end_y != wire.bend1_y);
  }
  // 终点的成本
  int occ = occupancy[wire.end_y][wire.end_x];
  cost += isAlready ? occ * occ : (occ + 1) * (occ + 1);
  
  return cost;
}

int main(int argc, char *argv[]) {
  const auto init_start = std::chrono::steady_clock::now();
  int pid;
  int nproc;

  // 初始化 MPI
  MPI_Init(&argc, &argv);
  // 获取进程排名
  MPI_Comm_rank(MPI_COMM_WORLD, &pid);
  // 获取进程总数
  MPI_Comm_size(MPI_COMM_WORLD, &nproc);

  std::string input_filename;
  double SA_prob = 0.1;
  int SA_iters = 5;
  char parallel_mode = '\0';
  int batch_size = 1;

  // 读取命令行参数
  int opt;
  while ((opt = getopt(argc, argv, "f:p:i:m:b:")) != -1) {
    switch (opt) {
      case 'f':
        input_filename = optarg;
        break;
      case 'p':
        SA_prob = atof(optarg);
        break;
      case 'i':
        SA_iters = atoi(optarg);
        break;
      case 'm':
        parallel_mode = *optarg;
        break;
      case 'b':
        batch_size = atoi(optarg);
        break;
      default:
        if (pid == ROOT) {
          std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -m parallel_mode -b batch_size\n";
        }

        MPI_Finalize();
        exit(EXIT_FAILURE);
    }
  }

  // 检查是否提供了必要的选项
  if (empty(input_filename) || SA_iters <= 0 || (parallel_mode != 'A' && parallel_mode != 'W') || batch_size <= 0) {
    if (pid == ROOT) {
      std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -m parallel_mode -b batch_size\n";
    }
    MPI_Finalize();
    exit(EXIT_FAILURE);
  }

  if (pid == ROOT) {
    std::cout << "Number of processes: " << nproc << '\n';
    std::cout << "Simulated annealing probability parameter: " << SA_prob << '\n';
    std::cout << "Simulated annealing iterations: " << SA_iters << '\n';
    std::cout << "Input file: " << input_filename << '\n';
    std::cout << "Parallel mode: " << parallel_mode << '\n';
    std::cout << "Batch size: " << batch_size << '\n';
  }

  int dim_x, dim_y, num_wires;
  std::vector<Wire> wires;
  std::vector<std::vector<int>> occupancy;

  if (pid == ROOT) {
      std::ifstream fin(input_filename);

      if (!fin) {
        std::cerr << "Unable to open file: " << input_filename << ".\n";
        exit(EXIT_FAILURE);
      }

      /* 从文件中读取网格维度和电线信息 */
      fin >> dim_x >> dim_y >> num_wires;
      // 对电线矢量做初始化，第一个弯折的地方就是起始点
      wires.resize(num_wires);
      for (auto& wire : wires) {
        fin >> wire.start_x >> wire.start_y >> wire.end_x >> wire.end_y;
        wire.bend1_x = wire.start_x;
        wire.bend1_y = wire.start_y;
      }
  }

  /* 初始化算法中所需的任何附加数据结构 */

  if (pid == ROOT) {
    const double init_time = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - init_start).count();
    std::cout << "Initialization time (sec): " << std::fixed << std::setprecision(10) << init_time << '\n';
  }

  const auto compute_start = std::chrono::steady_clock::now();

  /** 
   * (TODO)
   * 在这里实现电线布线算法
   * 可以将算法结构化为不同的函数
   * 使用 MPI 来并行化算法。 
   */
  // 预先为 occupancy 分配空间
  if(pid == ROOT) {
    occupancy.resize(dim_y, std::vector<int>(dim_x, 0));
  }
  // 开始迭代
  if(pid == ROOT) {
    for(int iter = 0; iter < SA_iters; iter++) {
      // 先计算一遍 occupancy 矩阵
      serial_cal_occupancy(occupancy, wires);
      for(Wire &wire : wires) {
      // 遍历所有已有的线 (已选择的线)
      // int start_x, start_y, end_x, end_y, bend1_x, bend1_y;
        // 1.计算当前路径的成本（若尚未知晓）。此路径即为当前的最短路径。
        Wire lowest_cost_wire = wire; 
        long long lowest_cost = compute_path_cost(lowest_cost_wire, occupancy, true);
        // 获取初始路径，防止在多次迭代后造成第一个弯折点 x, y 都不等于起始点 x,y
        Wire initial_wire = wire;
        initial_wire.bend1_x = wire.start_x;
        initial_wire.bend1_y = wire.start_y;
        // 2.考量所有先水平方向布线的路径。若其中任意路径的成本低于当前最短路径，则将其设为新的最短路径。
        for(int x = wire.start_x; x != wire.end_x; x += (wire.start_x < wire.end_x ? 1 : -1)) {
          if(x == wire.bend1_x) continue; // 跳过当前路径
          Wire horizontal_wire = initial_wire;
          horizontal_wire.bend1_x = x;
          long long horizontal_cost = compute_path_cost(horizontal_wire, occupancy, false);
          if (horizontal_cost < lowest_cost) {
            // lowest_cost_wire.bend1_x = horizontal_wire.bend1_x;
            // 这里必须要全部一起赋值，因为 bend1_y 可能在之前的迭代改变
            lowest_cost_wire = horizontal_wire;
            lowest_cost = horizontal_cost;
          }
        }
        // 3.考量所有先垂直方向布线的路径。若其中任意路径的成本低于当前最短路径，则将其设为新的最短路径。
        for(int y = wire.start_y; y != wire.end_y; y += (wire.start_y < wire.end_y ? 1 : -1)) {
          if(y == wire.bend1_y) continue; // 跳过当前路径
          Wire vertical_wire = initial_wire;
          vertical_wire.bend1_y = y;
          long long vertical_cost = compute_path_cost(vertical_wire, occupancy, false);
          if (vertical_cost < lowest_cost) {
            // lowest_cost_wire.bend1_y = vertical_wire.bend1_y;
            // 这里必须要全部一起赋值，因为 bend1_x 可能在 step2 改变
            lowest_cost_wire = vertical_wire;
            lowest_cost = vertical_cost;
          }
        }
        // if(wire.start_x == 1613 && wire.start_y == 2408) {
        //   std::cout << "Iter " << iter << ": Wire from (" << wire.start_x << "," << wire.start_y 
        //   << ") to (" << wire.bend1_x << "," << wire.bend1_y << ") to (" 
        //   << wire.end_x << "," << wire.end_y << ") updated to bend at (" 
        //   << lowest_cost_wire.bend1_x << "," << lowest_cost_wire.bend1_y << ") with cost " << lowest_cost << '\n';
        // }
        // 4. 用最短路径线更新线矢量
        wire = lowest_cost_wire;
      }
    }
  }

  // 结束后还要计算一遍 occupancy 矩阵
  if(pid == ROOT) {
    serial_cal_occupancy(occupancy, wires);
  }

  if (pid == ROOT) {
    const double compute_time = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - compute_start).count();
    std::cout << "Computation time (sec): " << std::fixed << std::setprecision(10) << compute_time << '\n';

    /* 将电线和占用矩阵写入文件 */
    print_stats(occupancy);
    write_output(wires, num_wires, occupancy, dim_x, dim_y, nproc, input_filename);
  }

  // 清理
  MPI_Finalize();
}
