#!/usr/bin/perl

# Perl 通常被归类为解释型语言，但在执行前会有一个编译阶段：解释器会把源码解析并编译成内
# 部的 optree/字节码表示，然后解释器运行该表示。

# use 在编译时加载并可导入符号；与运行时加载的 require 不同。
use POSIX;
use Getopt::Std;

# my 声明的变量只在最近的块、文件或 eval 中可见（block scope）。块结束后变量被销毁
# 测试用例名称（四个难度级别）
my @test_names = ("easy", "medium", "hard", "extreme");
# 每个测试要运行的进程数集合
my @test_nprocs = (1, 2, 4, 8);


# 各测试在不同进程数下的目标最快时间（秒）
my %fast_times;
$fast_times{"easy"}{1} = 0.49;
$fast_times{"easy"}{2} = 0.31;
$fast_times{"easy"}{4} = 0.19;
$fast_times{"easy"}{8} = 0.13;
$fast_times{"medium"}{1} = 9.52;
$fast_times{"medium"}{2} = 6.12;
$fast_times{"medium"}{4} = 4.04;
$fast_times{"medium"}{8} = 3.41;
$fast_times{"hard"}{1} = 11.15;
$fast_times{"hard"}{2} = 7.08;
$fast_times{"hard"}{4} = 4.1;
$fast_times{"hard"}{8} = 3.1;
$fast_times{"extreme"}{1} = 46.81;
$fast_times{"extreme"}{2} = 28.95;
$fast_times{"extreme"}{4} = 15.2;
$fast_times{"extreme"}{8} = 11;

# 各测试的参考（较好）代价，用于成本评估
my $good_costs;
$good_costs{"easy"} = 122050;
$good_costs{"medium"} = 601213;
$good_costs{"hard"} = 1032198;
$good_costs{"extreme"} = 23613045;

# 每个测试的满分基准点数（不同难度权重不同）
my %scores;
$scores{"easy"} = 1;
$scores{"medium"} = 2;
$scores{"hard"} = 3;
$scores{"extreme"} = 4;

# 性能评分相关阈值设置
my $perf_points = 10;
my $min_perf_points = 1;
my $min_ratio = 0.1;
my $max_ratio = 5.0/6.0;       # 时间比率上限（用于满分判断）
my $max_ratio_cost = 9.0 / 10.0;# 成本比率上限（用于满分判断）

# 正确性标记、你的时间与成本记录
my %correct;

my %your_times;
my %your_costs;

# sub 是用来定义子例程（函数）的关键字。它把一段可复用的代码命名起来，以便在脚本其它地方调用。
# 使用说明（当参数错误或请求帮助时打印）
sub usage {
    printf STDERR "$_[0]";
    printf STDERR "Usage: $0 [-h] [-R] [-s SIZE]\n";
    printf STDERR "    -h         Print this message\n";
    printf STDERR "    -R         Use reference (CPU-based) renderer\n";
    printf STDERR "    -s SIZE    Set image size\n";
    die "\n";
}

getopts('hRs:');
if ($opt_h) {
    usage();
}

# 准备日志目录，先创建再清空旧日志
`mkdir -p logs`;
`rm -rf logs/*`;

print "\n";
print ("--------------\n");
my $hostname = `hostname`;
chomp $hostname;
print ("Running tests on $hostname\n");
print ("--------------\n");

# 主循环：对每个测试和每个进程数运行被测程序并记录输出
foreach my $test (@test_names) {
    foreach my $nproc (@test_nprocs) {
        print ("\nTest : $test with $nproc cores\n");
        # 使用 mpirun 运行程序，输出重定向到对应日志文件
        my @sys_stdout = system ("mpirun -np ${nproc} ./wireroute -f ./inputs/timeinput/${test}_4096.txt -p 0.1 -i 5 -b 8 -m A > ./logs/${test}_${nproc}.log");
        my $return_value  = $?;
        if ($return_value == 0) {
            print ("Correctness passed!\n");
            $correct{$test}{$nproc} = 1;  # 正确性通过
        }
        else {
            print ("Correctness failed ... Check ./logs/${test}_${nproc}.log\n");
            $correct{$test}{$nproc} = 0;  # 正确性失败
        }
        
        # 从日志中提取“Computation”行以获取运行时间（处理并留下数字）
        my $your_time = `grep Computation ./logs/${test}_${nproc}.log`;
        chomp($your_time);
        $your_time =~ s/^[^0-9]*//;
        $your_time =~ s/ ms.*//;

        print ("Your time : $your_time\n");
        $your_times{$test}{$nproc} = $your_time;

        # 目标最快时间（用于比较）
        $target = $fast_times{$test}{$nproc};
        print ("Target Time: $target\n");

        # 从日志中提取“cost”行以获取成本值（处理并留下数字）
        my $your_cost = `grep cost ./logs/${test}_${nproc}.log`;
        chomp($your_cost);
        $your_cost =~ s/^[^0-9]*//;
        $your_cost =~ s/ ms.*//;
        print ("Your cost : $your_cost\n");
        $your_costs{$test}{$nproc} = $your_cost;
        
        # 目标成本（用于比较）
        $target_cost = $good_costs{$test};
        print ("Target Cost: $target_cost\n");
        
    }
}

print "\n";
print ("------------\n");
print ("Score table:\n");
print ("------------\n");

# 打印表头
my $header = sprintf ("| %-18s | %-18s | %-18s | %-18s | %-18s | %-18s | %-18s |\n", "Test Name", "Core Num", "Target Time ", "Your Time", "Target Cost", "Your Cost", "Score");
my $dashes = $header;
$dashes =~ s/./-/g;
print $dashes;
print $header;
print $dashes;

my $total_score = 0;

# 计算每项测试的分数并打印表格行
foreach my $test (@test_names) {
    foreach my $nproc (@test_nprocs) {
        my $score;
        my $your_time = $your_times{$test}{$nproc};
        my $your_cost = $your_costs{$test}{$nproc};
        my $fast_time = $fast_times{$test}{$nproc};
        my $good_cost = $good_costs{$test};
        my $cost_score;
        my $time_score;
    
        if ($correct{$test}{$nproc}) {
              # 时间比率与成本比率的计算（目标/实际）
              $ratio = $fast_time/$your_time;
              $cost_ratio = $good_cost/$your_cost;
            if ($ratio >= $max_ratio) {
                # 时间接近或优于阈值，优先按成本判断是否给满分
                if ($cost_ratio >= $max_ratio_cost) {
                    $score = $scores{$test}; # 满分
                } else {
                    $score = $scores{$test} * $cost_ratio; # 根据成本折算得分
                }
                
            }
            else {
                # 时间未达到阈值，根据时间与成本的较低值给分
                if ($cost_ratio >= $max_ratio_cost) {
                    $score = $scores{$test} * $ratio; # 成本合格，用时间比率给分
                } else {
                    $cost_score = $scores{$test} * $cost_ratio;
                    $time_score = $scores{$test} * $ratio;
                    if ($cost_score > $time_score) {
                        $score = $time_score
                    } else {
                        $score = $cost_score
                    }
                }
                
            }
        }
        else {
            # 正确性失败：标注时间并记分为 0
            $your_time .= " (F)";
            $score = 0;
        }
    
        printf ("| %-18s | %-18s | %-18s | %-18s | %-18s | %-18s | %-18s |\n", "$test", "$nproc", "$fast_time", "$your_time","$good_cost", "$your_cost", "$score");
        $total_score += $score;
    }
}
print $dashes;
# 打印总分（总分上限为 40）
printf ("| %-18s   %-18s   %-18s  %-18s   %-18s | %-18s | %-18s |\n", "", "","", "", "", "Total score:",
    $total_score . "/" . 40);
print $dashes;
