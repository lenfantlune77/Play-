#include "MailBox.h"

bool CMailBox::IsPending() const
{
	return m_calls.size() != 0;
}

void CMailBox::WaitForCall()
{
	std::unique_lock callLock(m_callMutex);
	while(!IsPending())
	{
		m_waitCondition.wait(callLock);
	}
}

void CMailBox::WaitForCall(unsigned int timeOut)
{
	std::unique_lock callLock(m_callMutex);
	if(IsPending()) return;
	m_waitCondition.wait_for(callLock, std::chrono::milliseconds(timeOut));
}

void CMailBox::FlushCalls()
{
	SendCall([]() {}, true);
}

std::atomic<int> CMailBox::g_waitingSenders = 0;

void CMailBox::SendCall(const FunctionType& function, bool waitForCompletion)
{
	std::future<void> future;

	{
		MESSAGE message;
		message.function = function;

		if(waitForCompletion)
		{
			message.promise = std::make_unique<std::promise<void>>();
			future = message.promise->get_future();
		}

		std::lock_guard callLock(m_callMutex);
		m_calls.push_back(std::move(message));
	}

	m_waitCondition.notify_all();

	if(waitForCompletion)
	{
		g_waitingSenders++;
		future.wait();
		g_waitingSenders--;
	}
}

void CMailBox::SendCall(FunctionType&& function)
{
	{
		MESSAGE message;
		message.function = std::move(function);

		std::lock_guard callLock(m_callMutex);
		m_calls.push_back(std::move(message));
	}

	m_waitCondition.notify_all();
}

void CMailBox::ReceiveCall()
{
	MESSAGE message;
	{
		std::lock_guard callLock(m_callMutex);
		if(!IsPending()) return;
		message = std::move(m_calls.front());
		m_calls.pop_front();
	}
	message.function();
	if(message.promise)
	{
		message.promise->set_value();
	}
}
