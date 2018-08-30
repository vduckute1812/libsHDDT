#pragma once

#include "ClaraMessage.h"


namespace clara
{


namespace msg
{

	class Messenger;

	struct MessengerData
	{
		//sbool operator==(const MessengerData& other) const { return messenger == other.messenger; }
		//Messenger* messenger;
		u64		typeMask;
		u64		messageMask;
	};

//typedef std::list<MessengerData >  ClaraMessengerList;
typedef boost::unordered_map<Messenger*, MessengerData>	 ClaraMessengerMap;

class Messenger
{
	DEFINE_RTTI_BASE_CLASS( Messenger );

public:
	Messenger();
	virtual ~Messenger();

	//blocks ALL messages for all messengers
	static void		BlockAllMessages(bool yes);

	//blocks messages for this messenger only
	void			BlockMessages(bool yes);

	//simulates a message sent by someone random. This calls OnMessageReceived
	void			Inject(const Message& msg);

	//sends the message to all listeners of this messenger
	virtual void	Send(Message& msg);

	void			SetReceivesBroadcasts(bool yes, u64 typeMask = ~0ULL, u64 messageMask = ~0ULL);
	bool			IsReceivingBroadcasts() const;

	static u32		GetSentCount();
	static u32		GetReceivedCount();
	static u32		GetBlockedCount();
	static u32		GetFilteredCount();

protected:
	void			Forward(const Message& msg);
	void			Forward(Messenger* to, const Message& msg);

	void			ListenTo(const Messenger* other, u64 typeMask = ~0ULL, u64 messageMask = ~0ULL);
	void			DisconnectFrom(const Messenger* other);
	void			DisconnectFromAll();

	virtual void	OnMessageReceived(const Message& msg);
	virtual void	OnBroadcastReceived(const Message& msg);

	bool			AreMessagesBlocked() const;

private:
	Messenger(const Messenger& other) {}

private:
	void			AddListener(Messenger* listener, const MessengerData& listenerData) const;
	void			RemoveListener(Messenger* listener) const;

	static ClaraMessengerMap*		s_registeredMessengers;
	ClaraMessengerMap::iterator		m_registeredMessengersPos;

	AutoPtr<ClaraMessengerMap>	m_listeningTo;
	mutable AutoPtr<ClaraMessengerMap>	m_listeners;

	bool			m_isReceivingBroadcasts;

	int				m_blockMessages;
	static int		s_blockMessages;

	static u32		s_sentCount;
	static u32		s_receivedCount;
	static u32		s_blockedCount;
	static u32		s_filteredCount;

	// this variable controls whether we have to check listener when processing listeners list
	mutable bool m_searchForRemovedListeners;
};

//////////////////////////////////////////////////////////////////////////

inline u32 Messenger::GetSentCount()
{
	return s_sentCount;
}

//////////////////////////////////////////////////////////////////////////

inline u32 Messenger::GetReceivedCount()
{
	return s_receivedCount;
}

//////////////////////////////////////////////////////////////////////////

inline u32 Messenger::GetBlockedCount()
{
	return s_blockedCount;
}

//////////////////////////////////////////////////////////////////////////

inline u32 Messenger::GetFilteredCount()
{
	return s_filteredCount;
}

//////////////////////////////////////////////////////////////////////////

inline Messenger::Messenger()
{
	m_isReceivingBroadcasts = false;
	m_searchForRemovedListeners = false;
}

//////////////////////////////////////////////////////////////////////////

inline Messenger::~Messenger()
{
	DisconnectFromAll();
	if (m_listeners.get())
	{
		while( !m_listeners->empty())
		{
			ClaraMessengerMap::iterator it = m_listeners->begin();
			Messenger* ptr = it->first;
			ptr->DisconnectFrom(this);
		}
		m_listeners->clear();
	}

	SetReceivesBroadcasts(false);
}

//////////////////////////////////////////////////////////////////////////

inline bool Messenger::AreMessagesBlocked() const
{
	return (s_blockMessages + m_blockMessages) != 0;
}


//////////////////////////////////////////////////////////////////////////

inline void Messenger::BlockAllMessages(bool yes)
{
	s_blockMessages += yes ? 1 : -1;
	PASSERT(s_blockMessages >= 0);
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::BlockMessages(bool yes)
{
	m_blockMessages += yes ? 1 : -1;
	PASSERT(m_blockMessages >= 0);
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::SetReceivesBroadcasts(bool yes, u64 typeMask, u64 messageMask)
{
	if (m_isReceivingBroadcasts != yes)
	{
		if (!s_registeredMessengers)
		{
			s_registeredMessengers = new(jet::mem::dontZero) ClaraMessengerMap;
		}

		MessengerData data;
		//data.messenger = this;
		data.typeMask = typeMask;
		data.messageMask = (messageMask & msg::Forward::MASK); //exclude the forward mask
		if (yes)
		{
			if (!m_isReceivingBroadcasts)
			{
				s_registeredMessengers->insert(s_registeredMessengers->end(), std::make_pair(this, data));
			}
		}
		else
		{
			if (m_isReceivingBroadcasts)
			{
				//find and delete ?
				ClaraMessengerMap::iterator it = s_registeredMessengers->find(this);
				if (it != s_registeredMessengers->end())
				{
					s_registeredMessengers->erase(it);
				}
			}
		}

		m_isReceivingBroadcasts = yes;
	}
}

//////////////////////////////////////////////////////////////////////////

inline bool Messenger::IsReceivingBroadcasts() const
{
	return m_isReceivingBroadcasts;
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::OnMessageReceived(const Message& msg)
{}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::OnBroadcastReceived(const Message& msg)
{}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::Forward(const Message& msg)
{
	if (msg.IsPrivate() || !m_listeners.get() || m_listeners->empty())
		return;

	if (msg.GetSender() == this && msg.GetForwarder() == this)
		return;

	//send to listeners
	if (AreMessagesBlocked())
	{
		s_blockedCount++;
		return;
	}

	Message m(msg);
	m.SetForwarder(this);

	PASSERT(m.HasMessageType());

	s_sentCount++;
	// a message could modify the listeners list breaking the iterators, use a cloned list to iterate
	m_searchForRemovedListeners = false;
	// copy !
	ClaraMessengerMap tmp = *m_listeners;
	ClaraMessengerMap::iterator it = tmp.begin();
	while (it != tmp.end())
	{
		ClaraMessengerMap::iterator next = it;
		next++;

		Messenger* msgr = it->first;
		const MessengerData& listenerData = it->second;
		// check if listener is still valid
		if (!m_searchForRemovedListeners || m_listeners->find(msgr) != m_listeners->end())
		{
			if (msg.GetSender() != msgr && msg.GetForwarder() != msgr)
			{
				if ((msg.GetType() & listenerData.typeMask) && (msg.GetMessage() & listenerData.messageMask))
				{
					s_receivedCount++;
					msgr->OnMessageReceived(m);
				}
				else
				{
					s_filteredCount++;
				}
			}
		}
		else
		{
			// listener has been erased
			//...
		}
		it = next;
	}

	//do not broadcast message - it was already broadcasted once
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::Forward(Messenger* to, const Message& msg)
{
	if (msg.IsPrivate())
		return;

	if (msg.GetSender() == this && msg.GetForwarder() == this)
		return;

	//send to listeners
	if (AreMessagesBlocked())
	{
		s_blockedCount++;
		return;
	}

	Message m(msg);
	m.SetForwarder(this);

	s_sentCount++;
	s_receivedCount++;

	PASSERT(m.HasMessageType());

	to->OnMessageReceived(m);

	//do not broadcast message - it was already broadcasted once
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::Send(Message& msg)
{
	if (AreMessagesBlocked())
	{
		s_blockedCount++;
		return;
	}

	if (!m_listeners.get() && !s_registeredMessengers)
		return;

	Message m(msg);
	m.SetSender(this);
	PASSERT(m.HasMessageType());

	s_sentCount++;

	m_searchForRemovedListeners = false;

	//send to listeners
	if (m_listeners.get())
	{
		// a message could modify the listeners list breaking the iterators, use a cloned list to iterate
		ClaraMessengerMap tmp = *m_listeners;
		ClaraMessengerMap::iterator it = tmp.begin();
		while (it != tmp.end())
		{
			ClaraMessengerMap::iterator next = it;
			next++;

			Messenger* msgr = it->first;
			const MessengerData& listenerData = it->second;
			// check if listener is still valid
			if (!m_searchForRemovedListeners || m_listeners->find(msgr) != m_listeners->end())
			{
				if (msg.GetSender() != msgr)
				{
					if ((msg.GetType() & listenerData.typeMask) && (msg.GetMessage() & listenerData.messageMask))
					{
						s_receivedCount++;
						msgr->OnMessageReceived(m);
					}
					else
					{
						s_filteredCount++;
					}
				}
			}
			else
			{
				// listener has been erased
				//...
			}
			it = next;
		}
	}

	//broadcast message
	if (s_registeredMessengers)
	{
		for (ClaraMessengerMap::iterator it = s_registeredMessengers->begin(), end = s_registeredMessengers->end(); it != end; ++it)
		{
			Messenger* msgr = it->first;
			const MessengerData& listenerData = it->second;
			if ((msg.GetType() & listenerData.typeMask) && (msg.GetMessage() & listenerData.messageMask))
			{
				msgr->OnBroadcastReceived(m);
			}
			else
			{
				//s_filteredCount++;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::Inject(const Message& msg)
{
	if (AreMessagesBlocked())
	{
		s_blockedCount++;
		return;
	}

	Message m(msg);
	m.SetSender(this);
	PASSERT(m.HasMessageType());

	s_sentCount++;
	s_receivedCount++;
	OnMessageReceived(m);
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::ListenTo(const Messenger* other, u64 typeMask /* = ~0u */, u64 messageMask /* = ~0u */)
{
	if (!other)
		return;

	if (!m_listeningTo.get())
	{
		m_listeningTo.reset(new(jet::mem::dontZero) ClaraMessengerMap);
	}

	MessengerData data;
	//data.messenger = (Messenger*)other;
	data.typeMask = typeMask;
	data.messageMask = (messageMask & msg::Forward::MASK); //exclude the forward mask

	//ClaraMessengerList::const_iterator it = std::find(m_listeningTo->begin(), m_listeningTo->end(), data);
	ClaraMessengerMap::const_iterator it = m_listeningTo->find((Messenger*)other);
	//already listening ?
	if (it != m_listeningTo->end())
		return;

	//m_listeningTo->push_back(data);
	m_listeningTo->insert(std::make_pair((Messenger*)other, data));

	//data.messenger = this;
	other->AddListener(this, data);
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::DisconnectFrom(const Messenger* other)
{
	if (!other || !m_listeningTo.get())
		return;

	//MessengerData data;
	//data.messenger = (Messenger*)other;
	ClaraMessengerMap::iterator it = m_listeningTo->find((Messenger*)other);
	if (it != m_listeningTo->end())
	{
		m_listeningTo->erase(it);
		other->RemoveListener(this);
	}
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::DisconnectFromAll()
{
	if (m_listeningTo.get())
	{
		for (ClaraMessengerMap::const_iterator it = m_listeningTo->begin(); it != m_listeningTo->end(); ++it)
		{
			//ms dont call this funtion to do lots of checks and remove one by one ..
			//DisconnectFrom(m_listeningTo->begin()->messenger);
			Messenger* ptr = it->first;
			ptr->RemoveListener(this);
		}
		//remove everything at once !
		m_listeningTo->clear();
	}
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::AddListener(Messenger* listener, const MessengerData& listenerData) const
{
	if (!m_listeners.get())
	{
		m_listeners.reset(new(jet::mem::dontZero) ClaraMessengerMap);
	}


	m_listeners->insert(std::make_pair(listener, listenerData));
	//m_listeners->push_back(listenerData);
}

//////////////////////////////////////////////////////////////////////////

inline void Messenger::RemoveListener(Messenger* listener) const
{
	if (m_listeners.get())
	{
		//MessengerData data;
		//data.messenger = listener;
		//ClaraMessengerList::iterator it = std::find(m_listeners->begin(), m_listeners->end(), data);
		ClaraMessengerMap::const_iterator it = m_listeners->find(listener);
		if (it != m_listeners->end())
		{
			m_listeners->erase(it);
			m_searchForRemovedListeners = true;
		}
	}
}


}


}
