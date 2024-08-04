#include <cactus_rt/ros2/app.h>
#include <cactus_rt/rt.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int64.hpp>
#include <thread>
#include <type_traits>

using cactus_rt::CyclicThread;
using cactus_rt::ros2::App;
using cactus_rt::ros2::StampedValue;

struct RealtimeData {
  int64_t data;

  RealtimeData() = default;
  explicit RealtimeData(int64_t d) : data(d) {}
};
using RosData = std_msgs::msg::Int64;

template <>
struct rclcpp::TypeAdapter<RealtimeData, RosData> {
  using is_specialized = std::true_type;
  using custom_type = RealtimeData;
  using ros_message_type = RosData;

  static void convert_to_ros_message(const custom_type& source, ros_message_type& destination) {
    destination.data = source.data;
  }

  static void convert_to_custom(const ros_message_type& source, custom_type& destination) {
    destination.data = source.data;
  }
};

class RealtimeROS2SubscriberThread : public CyclicThread, public cactus_rt::ros2::Ros2ThreadMixin {
  int64_t loop_counter_ = 0;

  std::shared_ptr<cactus_rt::ros2::SubscriptionLatest<RealtimeData, RosData>> subscription_;

  static cactus_rt::CyclicThreadConfig CreateThreadConfig() {
    cactus_rt::CyclicThreadConfig thread_config;
    thread_config.period_ns = 1'000'000;
    thread_config.cpu_affinity = std::vector<size_t>{2};
    thread_config.SetFifoScheduler(80);

    // thread_config.tracer_config.trace_sleep = true;
    thread_config.tracer_config.trace_wakeup_latency = true;
    return thread_config;
  }

 public:
  RealtimeROS2SubscriberThread(const char* name) : CyclicThread(name, CreateThreadConfig()) {}

  void InitializeForRos2() override {
    subscription_ = ros2_adapter_->CreateSubscriptionForLatestValue<RealtimeData, RosData>("/hello", rclcpp::QoS(10));
  }

  int64_t GetLoopCounter() const {
    return loop_counter_;
  }

 protected:
  bool Loop(int64_t /*now*/) noexcept final {
    loop_counter_++;
    if (loop_counter_ % 10 == 0) {
      StampedValue<RealtimeData> data;

      {
        const auto span = Tracer().WithSpan("ReadLatest");
        data = subscription_->ReadLatest();
      }

      LOG_INFO(Logger(), "Loop {}: {} {}", loop_counter_, data.id, data.value.data);
    }
    return false;
  }
};

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);

  App app("RTROS2Subscriber");
  app.StartTraceSession("build/subscriber.perfetto");

  auto thread = app.CreateROS2EnabledThread<RealtimeROS2SubscriberThread>("RTROS2SubscriberThread");
  app.RegisterThread(thread);

  constexpr unsigned int time = 60;
  std::cout << "Testing RT loop for " << time << " seconds.\n";

  app.Start();

  std::this_thread::sleep_for(std::chrono::seconds(time));

  app.RequestStop();
  app.Join();

  std::cout << "Number of loops executed: " << thread->GetLoopCounter() << "\n";
  return 0;
}
