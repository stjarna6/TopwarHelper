#pragma once

#include <QtNetwork>
#include <QRegularExpression>
#include <variant>
#include "common.h"

using QRegExp = QRegularExpression;

namespace HttpRqst {

void init();

QByteArray GetUserAgentPC();

/**
 * Returns the thread-local QNetworkAccessManager instance of current thread.
 */
QNetworkAccessManager* getNamInst();

/// Post data with content-type set to application/x-www-form-urlencoded.
/// The QNetworkAccessManager instance used is from getNamInst().
QNetworkReply* postUrlEncoded(QNetworkRequest rqst, const QByteArray &data);

/// Post data with content-type set to application/json.
/// The QNetworkAccessManager instance used is from getNamInst().
QNetworkReply* postJson(QNetworkRequest rqst, const QByteArray &data);

/// Post json obj.
/// The QNetworkAccessManager instance used is from getNamInst().
QNetworkReply* postJson(QNetworkRequest rqst, const QJsonObject &obj);



inline int getStatusCode(QNetworkReply *reply) {
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

inline QJsonObject toJson(QNetworkReply *reply) {
    QJsonDocument t = QJsonDocument::fromJson(reply->readAll());
    return t.object();
}




class RqstError {
public:
    enum class Reason {
        Unspecified,
        NetworkError,
        BadHttpStatusCode,
        BadApiResponse
    };
    using enum RqstError::Reason;

    const Reason reason;
    const QString detail;

    QString getDescription() const;

    RqstError()
        : reason{Reason::Unspecified}, detail{} {}

    RqstError(Reason reason, QString detail)
        : reason{reason}, detail{detail} {}
};

unique_ptr<RqstError> makeStatusCodeError(int code, QString reasonPhase);
unique_ptr<RqstError> makeNetworkError(QString errorString);
unique_ptr<RqstError> makeApiRespError(QString errorString);

/**
 * Checks http status code and then reply->error().
 * Returns error if any. Otherwise, returns nullptr.
 *
 * Note: Although reply->error() covers the case that http status code is not ok,
 * a separate check for http status code is made first to provide better error message.
 */
unique_ptr<RqstError> checkReplyError(QNetworkReply *reply);




template <class T, class E = unique_ptr<RqstError>>
    requires std::move_constructible<std::remove_cv_t<T>>
             && std::move_constructible<std::remove_cv_t<E>>
class Expected {
    std::variant<T, E> v;

public:
    Expected(std::remove_cv_t<T>&& val): v{std::move(val)} {}
    Expected(std::remove_cv_t<E>&& err): v{std::move(err)} {}

    bool hasValue() const { return std::holds_alternative<T>(v); }
    bool hasError() const { return std::holds_alternative<E>(v); }
    const T& value() const { return std::get<T>(v); }
    const E& error() const { return std::get<E>(v); }
    T& value() { return std::get<T>(v); }
    E& error() { return std::get<E>(v); }
    T&& takeValue() { return std::move(std::get<T>(v)); }
    E&& takeError() { return std::move(std::get<E>(v)); }
};



/*
Example:
```cpp
auto rqstFunc1(T0 a) { return RqstHandler<T1>{makeRqst1(a), processFunc1}; }
auto rqstFunc2(T1 a) { return RqstHandler<T2>{makeRqst2(a), processFunc2}; }
auto rqstFunc3(T2 a) { return RqstHandler<T3>{makeRqst3(a), processFunc3}; }
auto rqst(T0 arg) -> RqstHandler<T3> {
    return rqstFunc1(arg)
            .thenRqst(rqstFunc2)
            .thenRqst(rqstFunc3)
    ;
}

void SomeClass::someFunc() {
    auto rqstHandler = rqst(arg);
    rqstHandler.whenFinished(succCb, failCb);
    HttpRqst::AbortHandler abortHandler{rqstHandler};
    this->abortHandler = std::move(abortHandler);
    return;
}
```
If `abortHandler.abort()` is called before reply finishes, neither succCb nor failCb will be called.

`AbortHandler::abort()` is called in its destructor. Therefore the request will be
automatically aborted when the container instance of AbortHandler get deleted.



What is happening inside, if all requests succeed?

After setting up:

  abortHandler ╌╌╌╌╌╌╌╌╌╌╌╌╌┐
                            ╎
                            V
          ::finished ┏━━━━━━━━━━━━━━━━┓
          ┌╌╌╌╌╌╌╌╌╌╌┃ QNetworkReply* ┃<╌╌┬╌╌╌┬╌╌╌┐
      SLOT╎          ┃ value = reply1 ┃   ╎   ╎   ╎
          V          ┗━━━━━━━━━━━━━━━━┛   ╎   ╎   ╎
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━━╎━━━╎━━━━━━━━━┓
  ┃ shared_ptr<RqstHandler<T1>::Impl>     ╎   ╎   ╎         ┃
  ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘   ╎   ╎         ┃
  ┃ - processFunc1:(QNetReply*)->Expected<T1> ╎   ╎         ┃
  ┃ - continuation1                           ╎   ╎         ┃
  ┃   - rqstFunc2: (T1) -> RqstHandler<T2>    ╎   ╎         ┃
  ┃   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━━╎━━━━━┓   ┃
  ┃   ┃ shared_ptr<RqstHandler<T2>::Impl>     ╎   ╎     ┃   ┃
  ┃   ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘   ╎     ┃   ┃
  ┃   ┃ - processFunc = EMPTY                     ╎     ┃   ┃
  ┃   ┃ - continuation2                           ╎     ┃   ┃
  ┃   ┃   - rqstFunc3: (T2) -> RqstHandler<T3>    ╎     ┃   ┃
  ┃   ┃   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━┓  ┃   ┃
  ┃   ┃   ┃ shared_ptr<RqstHandler<T3>::Impl>     ╎  ┃  ┃   ┃
  ┃   ┃   ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤  ┃  ┃   ┃
  ┃   ┃   ┃ - processFunc = EMPTY                 ╎  ┃  ┃   ┃
  ┃   ┃   ┃ - continuation3                       ╎  ┃  ┃   ┃
  ┃   ┃   ┃   - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘  ┃  ┃   ┃
  ┃   ┃   ┃   - succCb                               ┃  ┃   ┃
  ┃   ┃   ┃   - failCb                               ┃  ┃   ┃
  ┃   ┃   ┗━━━━━━━━━━━━━━━━━━━ continuationHolder3 ━━┛  ┃   ┃
  ┃   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━ continuationHolder2 ━━┛   ┃
  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ handler1.impl ━━┛

(continuationHolder3 is wrapped by rqstHandler before SomeClass::someFunc() returns)



After reply1 is finished:
1. processFunc1 is called. Assume that it successfully returns T2 (wrapped by Expected<T2>)
2. rqstFunc2 is called, which returns handler2 of type RqstHandler<T2>
    (Note that handler2.impl->pReply is new created)
3. *pReply is set to reply2
4. pReply and continuation2 are move-assigned from continuationHolder2 to handler2
5. handler1.impl and continuationHolder2 are deleted

  abortHandler ╌╌╌╌╌╌╌╌╌╌╌╌╌┐
                            ╎
                            V
          ::finished ┏━━━━━━━━━━━━━━━━┓
          ┌╌╌╌╌╌╌╌╌╌╌┃ QNetworkReply* ┃<╌╌╌╌╌╌┬╌╌╌┐
      SLOT╎          ┃ value = reply2 ┃       ╎   ╎
          V          ┗━━━━━━━━━━━━━━━━┛       ╎   ╎
      ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━━╎━━━━━┓
      ┃ shared_ptr<RqstHandler<T2>::Impl>     ╎   ╎     ┃
      ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘   ╎     ┃
      ┃ - processFunc2:(QNetReply*)->Expected<T2> ╎     ┃
      ┃ - continuation2                           ╎     ┃
      ┃   - rqstFunc3: (T2) -> RqstHandler<T3>    ╎     ┃
      ┃   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━┓  ┃
      ┃   ┃ shared_ptr<RqstHandler<T3>::Impl>     ╎  ┃  ┃
      ┃   ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤  ┃  ┃
      ┃   ┃ - processFunc = EMPTY                 ╎  ┃  ┃
      ┃   ┃ - continuation3                       ╎  ┃  ┃
      ┃   ┃   - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘  ┃  ┃
      ┃   ┃   - succCb                               ┃  ┃
      ┃   ┃   - failCb                               ┃  ┃
      ┃   ┗━━━━━━━━━━━━━━━━━━━ continuationHolder3 ━━┛  ┃
      ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ handler2.impl ━━┛



After reply2 is finished:

  abortHandler ╌╌╌╌╌╌╌╌╌╌╌╌╌┐
                            ╎
                            V
          ::finished ┏━━━━━━━━━━━━━━━━┓
            ┌╌╌╌╌╌╌╌╌┃ QNetworkReply* ┃<╌╌╌╌╌╌╌╌╌╌╌┐
        SLOT╎        ┃ value = reply3 ┃            ╎
            V        ┗━━━━━━━━━━━━━━━━┛            ╎
          ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╎━━┓
          ┃ shared_ptr<RqstHandler<T3>::Impl>      ╎  ┃
          ┃ - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤  ┃
          ┃ - processFn3:(QNetReply*)->Expected<T3>╎  ┃
          ┃ - continuation3                        ╎  ┃
          ┃   - pReply ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┘  ┃
          ┃   - succCb                                ┃
          ┃   - failCb                                ┃
          ┗━━━━━━━━━━━━━━━━━━━━━━━━━ handler3.impl ━━━┛


At last, reply3 is finished and succCb get called.



If error is returned by any processFunc, following continuation callbacks
are called one by one (in a nested way) and at last failCb is called.
*/


// class RqstHandler provides convenient interfaces to make a sequence of
// requests where one request depends on the previous one.
template <class T>
class RqstHandler {
    struct Impl {
        shared_ptr<QNetworkReply*> pReply;
        std::function<Expected<T> (QNetworkReply*)> processFunc;
        std::function<void(Expected<T>)> continuation;

        Impl(shared_ptr<QNetworkReply*> pReply)
            : pReply{std::move(pReply)}
        {}

        template<class Function>
        Impl(QNetworkReply *r, Function &&processFunc)
            : pReply{make_shared<QNetworkReply*>(r)},
              processFunc{std::forward<Function>(processFunc)}
        {}
    };
    shared_ptr<Impl> impl;


    using ResultType = T;

    template <class Function>
    using ContResultType = typename std::invoke_result_t<std::decay_t<Function>, T>::ResultType;

    friend class AbortHandler;

    template <class U>
    friend class RqstHandler;

    RqstHandler(shared_ptr<Impl> &&impl)
        : impl{std::move(impl)} {}

public:
    RqstHandler() = default;

    template<class Function>
    RqstHandler(QNetworkReply *r, Function &&processFunc) {
        impl = make_shared<Impl>(r, std::forward<Function>(processFunc));
        QObject::connect(r, &QNetworkReply::finished, r, [impl=this->impl] {
            QNetworkReply *r = *(impl->pReply);
            if (r == nullptr) {
                // reply is aborted. see AbortHandler::abort()
                return;
            }
            Expected<T> ret = impl->processFunc(r);
            if (impl->continuation) {
                impl->continuation(std::move(ret));
            } else {
                // set *pReply to nullptr so that AbortHandler knows reply has finished
                *(impl->pReply) = nullptr;
            }
        });
    }

    // Function: (T result) -> RqstHandler<U>
    template<class Function>
    auto thenRqst(Function &&func) {
        using U = ContResultType<Function>;
        auto contHolder = make_shared<typename RqstHandler<U>::Impl>(impl->pReply);
        impl->continuation = [
            contHolder,
            func=std::forward<Function>(func)
        ] (Expected<T> res) {
            if (res.hasError()) {
                contHolder->continuation(Expected<U>{res.takeError()});
                return;
            }
            // make next request
            auto cont = func(res.takeValue()).impl;
            QNetworkReply* reply = *(cont->pReply);

            cont->pReply = std::move(contHolder->pReply);
            *(cont->pReply) = reply;
            cont->continuation = std::move(contHolder->continuation);
        };

        return RqstHandler<U>{std::move(contHolder)};
    }

    // Function: void (Expected <T>)
    template <class Function>
    void whenFinished(Function &&func) {
        impl->continuation = [
            pReply=impl->pReply,
            func=std::forward<Function>(func)
        ] (Expected<T> res) {
            // set *pReply to nullptr so that AbortHandler knows reply has finished
            *pReply = nullptr;
            func(std::move(res));
        };
    }

    template <class SuccCb, class FailCb>
    void whenFinished(SuccCb &&succCb, FailCb &&failCb) {
        impl->continuation = [
            pReply=impl->pReply,
            succCb=std::forward<SuccCb>(succCb),
            failCb=std::forward<FailCb>(failCb)
        ] (Expected<T> res) {
            // set *pReply to nullptr so that AbortHandler knows reply has finished
            *pReply = nullptr;
            if (res.hasError()) {
                const RqstError &err = *res.error();
                failCb(err);
            } else {
                succCb(res.takeValue());
            }
        };
    }
};



// This class is move only (copy constructor is deleted)
class AbortHandler {
    shared_ptr<QNetworkReply*> pReply;

public:
    AbortHandler() = default;

    template <class T>
    AbortHandler(const RqstHandler<T> &h)
        : pReply{h.impl->pReply} {}

    ~AbortHandler() { abort(); }

    void abort() {
        if (pReply == nullptr) {
            // invalid AbortHandler created by default constructor
            return;
        }
        QNetworkReply *r = *pReply;
        if (r == nullptr) {
            // already finished or aborted
            return;
        }
        // set *pReply to nullptr so that RqstHandler knows reply is aborted
        *pReply = nullptr;
        r->abort();
    }

    AbortHandler& operator= (AbortHandler &&other) {
        if (this != (&other)) {
            pReply = std::move(other.pReply);
        }
        return *this;
    }

    // delete copy constructor
    AbortHandler(const AbortHandler &) = delete;

    // use default move constructor
    AbortHandler(AbortHandler &&) = default;

    template <class FinishCallback>
    void bindRqst(QNetworkReply *r, FinishCallback &&onFinished) {
        pReply = make_shared<QNetworkReply*>(r);
        QObject::connect(r, &QNetworkReply::finished, r, [
                                                             pReply=pReply, onFinished=std::forward<FinishCallback>(onFinished)
        ] {
            QNetworkReply *r = *pReply;
            if (r == nullptr) {
                // reply is aborted.
                return;
            }
            onFinished(r);
            *pReply = nullptr;
        });
    }

    template <class T>
    void bindRqst(const RqstHandler<T> &h) {
        pReply = h.impl->pReply;
    }

    bool isValid() {
        return (pReply != nullptr) && (*pReply != nullptr);
    }
};

} // END namespace HttpRqst
