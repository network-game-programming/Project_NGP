#include "pch.h"
#include "MainServer.h"
#include "Scene.h"
#include "Spawn.h"
#include "Monster.h"
#include "GameObject.h"
#include "MainScene_1.h"
#include "MainScene_2.h"

vector<Scene*> MainServer::m_vecScene;

MainServer::MainServer()
{
}

MainServer::~MainServer()
{
	Release();
}

bool MainServer::Initialize()
{
	// 유니코드 한글 출력을 위함
	wcout.imbue(std::locale("korean"));
	// 윈속 초기화
	if (0 != WSAStartup(MAKEWORD(2, 2), &m_WSAData))
	{
		error_display("윈속 초기화 실패");
		return false;
	}
		
	// listen 소켓 생성
	m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0,
		WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_listenSocket)
	{ 
		error_display("리슨 소켓 생성 실패");
		return false;
	}
	// 서버 주소 소켓 구조체
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	// 지역 주소, 포트 할당
	if (0 != ::bind(m_listenSocket, (sockaddr*)&serverAddr,
		sizeof(SOCKADDR_IN)))
	{ 
		error_display("리슨 소켓 바인드 실패");
		return false;
	}
	// 리슨 소켓 상태 리슨으로 변경
	if (0 != listen(m_listenSocket, SOMAXCONN))
	{ 
		error_display("리슨 소켓 상태 변경 실패");
		return false;
	}
	
	memset(&m_clientAddr, 0, sizeof(SOCKADDR_IN));
	
	// iocp 생성
	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	if (NULL == g_iocp)
	{ 
		error_display("IOCP 생성 실패");
		return false;
	}

	return true;
}

bool MainServer::Running()
{
	SYSTEM_INFO sys_info;
	GetSystemInfo(&sys_info);

	// cpu thread 갯수를 가지고 온다.
	int thread_count = sys_info.dwNumberOfProcessors;

	// cpu thread 갯수만큼 서버처리를 하겠다.
	for (int i = 0; i < thread_count - 1; ++i)
	{
		m_vecThread.emplace_back(do_worker);
	}
	m_vecThread.emplace_back(do_scene);

	int addrLen = sizeof(SOCKADDR_IN);
	g_id = 1;
	while (true)
	{
		m_clientSocket = accept(m_listenSocket, (sockaddr*)&m_clientAddr, &addrLen);
		int user_id = g_id++;

		g_mapClient[user_id] = new SOCKET_INFO;
		memset(&g_mapClient[user_id]->over_info.over, 0x00, sizeof(WSAOVERLAPPED));
		g_mapClient[user_id]->socket = m_clientSocket;
		g_mapClient[user_id]->over_info.wsaBuffer.len = MAX_BUFFER;
		g_mapClient[user_id]->over_info.wsaBuffer.buf = g_mapClient[user_id]->over_info.buffer;
		g_mapClient[user_id]->over_info.is_recv = true;

		printf("[클라이언트 접속] ID : %d, IP : %s, PORT : %d, SOCKET : %d\n",  
			user_id, inet_ntoa(m_clientAddr.sin_addr), ntohs(m_clientAddr.sin_port), (int)m_clientSocket);

		memcpy(&g_mapClient[user_id]->addr_info, &m_clientAddr, sizeof(SOCKADDR_IN));

		m_flags = 0;
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_clientSocket), g_iocp, user_id, 0);

		// 나를 제외한 다른 클라이언트 이벤트 큐에 내가 추가되었음을 알리는 이벤트 추가
		EVENTINFO info;
		info.size = sizeof(info);
		info.type = SP_EVENT;
		info.event_state = EV_PUTOTHERPLAYER;
		for (auto& cl : g_mapClient)
		{
			if (cl.first == user_id) continue;

			info.id = user_id;
			info.scene_state = SCENE_MAIN_1;
			cl.second->event_lock.lock();
			cl.second->event_queue.push(info);
			cl.second->event_lock.unlock();

			info.id = cl.first;
			info.scene_state = cl.second->scene_state;
			g_mapClient[user_id]->event_lock.lock();
			g_mapClient[user_id]->event_queue.push(info);
			g_mapClient[user_id]->event_lock.unlock();
		}

		int result = WSARecv(m_clientSocket, &g_mapClient[user_id]->over_info.wsaBuffer,
			1, NULL, &m_flags, &(g_mapClient[user_id]->over_info.over), NULL);
		if (0 != result)
		{
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
			{
				error_display("WSARecv 함수 에러");
				return false;
			}
		}
	}
	return true;
}

void MainServer::Release()
{
	for (auto& t : m_vecThread)
	{
		t.join();
	}
	m_vecThread.clear();
	closesocket(m_listenSocket);
	WSACleanup();
}

void MainServer::error_display(const char * msg)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg << endl;
	wcout << L"Error : " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
}

void MainServer::do_worker()
{
	while (true)
	{
		DWORD byte;
		ULONG key;
		PULONG pKey = &key;
		WSAOVERLAPPED *pOver;

		// 통신 완료 상태의 소켓을 큐로 부터 가지고 온다.
		GetQueuedCompletionStatus(g_iocp, &byte, (PULONG_PTR)pKey,
			&pOver, INFINITE);

		SOCKET clientsocket = g_mapClient[key]->socket;

		if (0 == byte)
		{
			closesocket(clientsocket);
			printf("[클라이언트 종료] ID : %d, IP : %s, PORT : %d, SOCKET : %d\n",
				key, inet_ntoa(g_mapClient[key]->addr_info.sin_addr),
					 ntohs(g_mapClient[key]->addr_info.sin_port), (int)clientsocket);

			// 종료 작업
			EVENTINFO info;
			info.size = sizeof(info);
			info.type = SP_EVENT;
			info.id = key;
			info.event_state = EV_END;
			for (auto& cl : g_mapClient)
			{
				if (cl.first == key) continue;
				cl.second->event_lock.lock();
				cl.second->event_queue.push(info);
				cl.second->event_lock.unlock();
			}

			delete g_mapClient[key];
			g_mapClient[key] = nullptr;
			g_mapClient.erase(key);
			continue;
		}

		OVERLAPPED_INFO *over_info = reinterpret_cast<OVERLAPPED_INFO*>(pOver);

		if (true == over_info->is_recv)
		{
			// 패킷을 다 받을때

			// 클라이언트에서 받은 패킷을 처리
			ProcessPacket(key, over_info->buffer, byte);

			// 받았을때의 처리
			DWORD flags = 0;
			memset(&over_info->over, 0x00, sizeof(WSAOVERLAPPED));
			WSARecv(clientsocket, &over_info->wsaBuffer, 1, 0, &flags, &over_info->over, NULL);
		}
		else
		{
			// 전송할때의 처리
			// Send할때 
			delete over_info;
		}
	}
}

void MainServer::do_scene()
{
	// cpu 진동수로 timedelta 계산
	LARGE_INTEGER cpuTick;
	LARGE_INTEGER fixTime;
	LARGE_INTEGER lastTime;
	LARGE_INTEGER frameTime;

	QueryPerformanceCounter(&frameTime);
	QueryPerformanceCounter(&fixTime);
	QueryPerformanceCounter(&lastTime);
	QueryPerformanceFrequency(&cpuTick);

	float TimeDelta = 0.f;
	float TimeAcc = 0.f;
	float FrameLimitTime = 1 / 60.f;

	// Scene을 초기화 한다.
	for (int i = SCENE_MENU; i < SCENE_END; ++i)
	{
		Scene* s = nullptr;

		switch (i)
		{
		case SCENE_MENU:
			break;
		case SCENE_TEST:
			break;
		case SCENE_MAIN_1:
			s = new MainScene_1;
			break;
		case SCENE_MAIN_2:
			s = new MainScene_2;
			break;
		}

		if (nullptr != s &&
			false == s->Initialize())
		{
			cout << "Scene 초기화 실패!" << endl;
			delete s;
			s = nullptr;
			return;
		}

		m_vecScene.emplace_back(s);
	}

	while (true)
	{
		QueryPerformanceCounter(&frameTime);
		
		if ((frameTime.QuadPart - lastTime.QuadPart) > cpuTick.QuadPart)
		{
			QueryPerformanceFrequency(&cpuTick);
			lastTime.QuadPart = frameTime.QuadPart;
		}
		
		TimeDelta = float(frameTime.QuadPart - fixTime.QuadPart) / cpuTick.QuadPart;
		fixTime = frameTime;

		TimeAcc += TimeDelta;

		if (FrameLimitTime <= TimeAcc)
		{
			for (auto& scene : m_vecScene)
			{
				if (nullptr != scene)
					scene->Update(FrameLimitTime);
			}
			TimeAcc = 0.f;
		}
	}
}

void MainServer::ProcessPacket(int id, void* buf, int recv_byte)
{
	char* packet = reinterpret_cast<char*> (buf);

#ifdef LOG_CHECK
	SOCKET_INFO* sockInfo = g_mapClient[id];
	printf("-----------------------------------------------------\n");
	printf("[수신] ID : %d, IP : %s, PORT : %d, SOCKET : %d\n",
		id, inet_ntoa(g_mapClient[id]->addr_info.sin_addr),
		ntohs(g_mapClient[id]->addr_info.sin_port), g_mapClient[id]->socket);
	printf("[수신 내역] ");
	switch (packet[2])
	{
	case SP_LOGIN_OK: 
		printf(" 로그인 요청");
		break;
	case SP_PLAYER:
		printf(" 클라이언트 플레이어 정보\n");
		break;
	case SP_OTHERPLAYER:
		printf(" 다른 클라이언트 플레이어 정보 요청");
		break;
	case SP_MONSTER:
		printf(" 몬스터 정보 요청");
		break;
	case SP_HIT:
		printf(" 공격 정보");
		break;
	case SP_GONEXT:
		printf(" 씬 전환 정보");
		break;
	case SP_EVENT:
		printf(" 이벤트 정보 요청");
		break;
	}
	printf(" | 수신 받은 바이트 크기 : %d[byte]\n", recv_byte);
	printf("-----------------------------------------------------\n\n");
#endif

	switch (packet[2])
	{
	case SP_PLAYER:
	{
		SPPLAYER *PlayerInfo = reinterpret_cast<SPPLAYER*> (buf);
		g_mapClient[id]->scene_state = PlayerInfo->scene_state;
		g_mapClient[id]->player_info.pos_x = PlayerInfo->info.pos_x;
		g_mapClient[id]->player_info.pos_y = PlayerInfo->info.pos_y;
		g_mapClient[id]->player_info.player_state = PlayerInfo->info.player_state;
		g_mapClient[id]->player_info.player_dir = PlayerInfo->info.player_dir;
	}
	break;
	case SP_HIT:
	{
		SPHIT* HitInfo = reinterpret_cast<SPHIT*> (buf);
		Scene* curScene = m_vecScene[g_mapClient[HitInfo->id]->scene_state];
		if (nullptr == curScene)
			return;
		Monster* DstMon = curScene->GetMapSpawn().find(HitInfo->monster_id)->second->GetMonster();
		if (nullptr == DstMon)
			return;
		DstMon->Hit(HitInfo->id, HitInfo->damage);
	}
	break;
	case SP_GONEXT:
	{
		SPGONEXT* GoNextInfo = reinterpret_cast<SPGONEXT*> (buf);

		EVENTINFO info;
		info.size = sizeof(info);
		info.type = SP_EVENT;
		for (auto& cl : g_mapClient)
		{
			if (cl.first == GoNextInfo->id) continue;

			// 다음으로 넘어갈 씬에 존재하는 유저들에게 이벤트 큐에 내가 추가되었음을 알린다.
			if (GoNextInfo->next_scene_state == cl.second->scene_state)
			{
				info.event_state = EV_PUTOTHERPLAYER;
				info.id = GoNextInfo->id;
				info.scene_state = GoNextInfo->next_scene_state;
				cl.second->event_lock.lock();
				cl.second->event_queue.push(info);
				cl.second->event_lock.unlock();
			}
			
			// 현재 씬에 존재하는 유저들에게 이벤트 큐에 내가 사라졌음을 알린다.
			if (GoNextInfo->cur_scene_state == cl.second->scene_state)
			{
				info.event_state = EV_END;
				info.id = GoNextInfo->id;
				info.scene_state = GoNextInfo->cur_scene_state;
				cl.second->event_lock.lock();
				cl.second->event_queue.push(info);
				cl.second->event_lock.unlock();
			}
		}
	}
	break;
	case SP_CHAT:
	{
		char* ChatInfo = reinterpret_cast<char*> (buf);
		short total_size = 0;
		memcpy(&total_size, ChatInfo, sizeof(short));
		short chat_start = sizeof(short) + sizeof(char) + sizeof(int);
		int chat_size = total_size - chat_start;

		EVENTINFO info;
		info.size = sizeof(info);
		info.type = SP_EVENT;
		info.event_state = EV_CHAT;
		memcpy(&info.id, &ChatInfo[3], sizeof(int));
		info.scene_state = g_mapClient[info.id]->scene_state;
		info.chat_size = chat_size;
		memcpy(&info.chat_buffer, &ChatInfo[7], info.chat_size);

		for (auto& cl : g_mapClient)
		{
			if (cl.first == info.id) continue;

			cl.second->event_lock.lock();
			cl.second->event_queue.push(info);
			cl.second->event_lock.unlock();
		}
	}
	break;
	}

	SendProcess(id, buf);
}

void MainServer::SendProcess(int send_id, void* buf)
{
	char* packet = reinterpret_cast<char*> (buf);

#ifdef LOG_CHECK
	SOCKET_INFO* sockInfo = g_mapClient[send_id];
	printf("-----------------------------------------------------\n");
	printf("[송신] ID : %d, IP : %s, PORT : %d, SOCKET : %d\n",
		send_id, inet_ntoa(g_mapClient[send_id]->addr_info.sin_addr),
		ntohs(g_mapClient[send_id]->addr_info.sin_port), g_mapClient[send_id]->socket);
	printf("[송신 내역] ");
	switch (packet[2])
	{
	case SP_LOGIN_OK:
		printf(" 로그인 정보");
		break;
	case SP_PLAYER:
		printf(" 클라이언트 플레이어 정보\n");
		break;
	case SP_OTHERPLAYER:
		printf(" 다른 클라이언트 플레이어 정보");
		break;
	case SP_MONSTER:
		printf(" 몬스터 정보");
		break;
	case SP_HIT:
		printf(" 공격 정보");
		break;
	case SP_GONEXT:
		printf(" 씬 전환 정보");
		break;
	case SP_EVENT:
		printf(" 이벤트 정보");
		break;
	}
#endif

	switch (packet[2])
	{
	case SP_LOGIN_OK:
	{
		SPLOGIN packet;
		packet.id = send_id;
		packet.size = sizeof(packet);
		packet.type = SP_LOGIN_OK;
		SendPacket(send_id, &packet, sizeof(packet));
	}
	break;
	case SP_OTHERPLAYER:
	{
		char tempBuffer[MAX_BUFFER];

		// size type을 넣은 패킷의 주소 시작 위치
		int startAddrPos = 2 + 1;
		int count = 0;
		for (auto& c : g_mapClient)
		{
			// 자신은 넣지 않는다.
			if (send_id == c.first)
				continue;

			SPOTHERPLAYERS info = SPOTHERPLAYERS{};
			info.id = c.first;
			info.scene_state = c.second->scene_state;
			info.info.pos_x = c.second->player_info.pos_x;
			info.info.pos_y = c.second->player_info.pos_y;
			info.info.player_state = c.second->player_info.player_state;
			info.info.player_dir = c.second->player_info.player_dir;

			memcpy((tempBuffer + startAddrPos + sizeof(SPOTHERPLAYERS) * count),
				&info, sizeof(SPOTHERPLAYERS));
			++count;
		}
		// size
		short size = sizeof(SPOTHERPLAYERS) * count + sizeof(short) + sizeof(char);
		memcpy(&tempBuffer[0], &size, sizeof(short));
		// type
		tempBuffer[2] = SP_OTHERPLAYER;

		SendPacket(send_id, &tempBuffer, size);
	}
	break;
	case SP_MONSTER:
	{
		SCENESTATE CurSceneState = (SCENESTATE)packet[3];

		Scene* CurScene = m_vecScene[CurSceneState];
		char tempBuffer[MAX_BUFFER];

		if (nullptr == CurScene)
		{
			int size = 2;
			memcpy(&tempBuffer[0], &size, sizeof(short));
			tempBuffer[2] = SP_MONSTER;
			SendPacket(send_id, &tempBuffer, 3);
			break;
		}

		// size type을 넣은 패킷의 주소 시작 위치
		int startAddrPos = 2 + 1;
		int count = 0;
		for (auto& s : CurScene->GetMapSpawn())
		{
			// packet struct
			SPMONSTER monster_info = SPMONSTER{};
			monster_info.monster_id = s.first;

			Monster* m = s.second->GetMonster();
			if (nullptr == m)
				continue;

			// monster info struct
			MONSTERINFO info = MONSTERINFO{};
			info.monster_type = m->GetMonType();
			info.monster_state = m->GetMonState();
			info.pos_x = m->GetInfo().Pos_X;
			info.pos_y = m->GetInfo().Pos_Y;
			info.monster_dir = m->GetDirection();

			monster_info.info = info;

			memcpy((tempBuffer + startAddrPos + sizeof(SPMONSTER) * count),
				&monster_info, sizeof(SPMONSTER));
			++count;
		}
		// size
		short size = short(sizeof(SPMONSTER) * count + sizeof(short) + sizeof(char));
		memcpy(&tempBuffer[0], &size, sizeof(short));
		// type
		tempBuffer[2] = SP_MONSTER;
		SendPacket(send_id, &tempBuffer, size);
	}
	break;
	case SP_EVENT:
	{
		g_mapClient[send_id]->event_lock.lock();
		if (g_mapClient[send_id]->event_queue.empty())
		{
			g_mapClient[send_id]->event_lock.unlock();
			EVENTINFO packet = EVENTINFO{ };
			packet.size = sizeof(packet);
			packet.event_state = EV_NONE;
			SendPacket(send_id, &packet, sizeof(packet));
			break;
		}

		const EVENTINFO& evInfo = g_mapClient[send_id]->event_queue.front();
		EVENTINFO packet = evInfo;
		g_mapClient[send_id]->event_queue.pop();
		g_mapClient[send_id]->event_lock.unlock();
		SendPacket(send_id, &packet, sizeof(packet));
	}
	break;
	}
}

void MainServer::SendPacket(int send_id, void* buf, int size)
{
	char* packet = reinterpret_cast<char*>(buf);
	int packet_size = size;

#ifdef LOG_CHECK
	printf(" | 송신한 바이트 크기 : %d[byte]\n", packet_size);
	printf("-----------------------------------------------------\n\n");
#endif

	OVERLAPPED_INFO* send_over = new OVERLAPPED_INFO;
	memset(send_over, 0x00, sizeof(OVERLAPPED_INFO));
	send_over->is_recv = false; // recv 아니다~
	memcpy(send_over->buffer, packet, packet_size);
	send_over->wsaBuffer.buf = send_over->buffer;
	send_over->wsaBuffer.len = packet_size;
	int result = WSASend(g_mapClient[send_id]->socket, &send_over->wsaBuffer, 1, 0, 0, &send_over->over, NULL);
	result = 0;
}
