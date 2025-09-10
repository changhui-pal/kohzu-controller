#pragma once
/**
 * Dispatcher.hpp
 *
 * 요청-응답 매칭 및 spontaneous(자발) 메시지 분배를 담당하는 디스패처.
 *
 * 책임:
 *  - 키(key) 기반으로 pending promise/future 관리
 *  - 응답 도착 시 pending을 찾아 fulfill
 *  - 타임아웃/종료 시 pending에 예외 설정
 *  - 자발 메시지(Spontaneous) 핸들러 등록 및 비동기 호출
 *
 * 스레드 안정성:
 *  - addPending / tryFulfill / removePendingWithException / cancelAllPendingWithException
 *    은 내부 mutex로 보호되어 스레드 안전하게 호출 가능.
 *  - registerSpontaneousHandler / notifySpontaneous 도 handlerMtx_로 보호됨.
 *
 * 사용 예:
 *   auto fut = dispatcher.addPending("RDP:1");
 *   writer.enqueue(...);
 *   auto resp = fut.get(); // 또는 wait_for(timeout)
 *
 * 참고:
 *  - Response 타입은 Parser.hpp에 정의되어 있으며, 여기에 포함되어 사용됩니다.
 */

#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Parser.hpp" // Response 정의

namespace kohzu::protocol {

class Dispatcher {
public:
    using SpontaneousHandler = std::function<void(const Response&)>;

    Dispatcher();
    ~Dispatcher();

    // addPending: key에 대한 promise를 등록하고 future를 반환.
    // 호출자는 반환된 future로 응답을 대기(wait/get)할 수 있음.
    // 만약 동일 키가 이미 존재하면 경고 로그(혹은 정책에 따라 다르게 처리)를 남기고 새 promise를 등록함.
    std::future<Response> addPending(const std::string& key);

    // tryFulfill: key에 해당하는 pending이 있으면 promise.set_value(response)를 수행하고 true 반환.
    // 없으면 false 반환.
    bool tryFulfill(const std::string& key, const Response& response);

    // removePendingWithException: 특정 키에 대해 예외를 설정(타임아웃 등).
    void removePendingWithException(const std::string& key, const std::string& message);

    // cancelAllPendingWithException: 모든 pending에 동일한 예외를 설정(예: 종료시).
    void cancelAllPendingWithException(const std::string& message);

    // spontaneous handler 등록 (핸들러는 Response 수신 시 비동기적으로 호출될 예정)
    void registerSpontaneousHandler(SpontaneousHandler h);

    // notifySpontaneous: 등록된 핸들러들에 대해 비동기 호출을 수행
    void notifySpontaneous(const Response& resp);

private:
    // pending map: key -> promise<Response>
    std::unordered_map<std::string, std::promise<Response>> pending_;
    std::mutex mtx_; // protects pending_

    // spontaneous handlers
    std::vector<SpontaneousHandler> spontaneousHandlers_;
    std::mutex handlerMtx_;
};

} // namespace kohzu::protocol
