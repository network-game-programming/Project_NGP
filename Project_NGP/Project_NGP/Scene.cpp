#include "stdafx.h"
#include "Scene.h"
#include "GameObject.h"
#include "Player.h"
#include "BlueSnail.h"
#include "Slime.h"
#include "Mushroom.h"
#include "GreenMush.h"

Scene::Scene()
{
	m_CamManager = GET_MANAGER<CameraManager>();
	m_ObjManager = GET_MANAGER<ObjectManager>();
	m_NetworkManager = GET_MANAGER<NetworkManager>();
}

Scene::~Scene()
{
}

bool Scene::InitializeNetwork(SCENESTATE curScene)
{
	m_curScene = curScene;

	// 큐로부터 받지 못했던 다른 플레이어 정보를 받는다.
	char otherInfo[MAX_BUFFER] = { 0, };
	if (true == m_NetworkManager->SendAndRecvOtherInfo(otherInfo))
	{
		short size = 0;
		memcpy(&size, otherInfo, sizeof(short));
		if (0 == size)
			return true;

		int startAddrPos = 2 + 1;
		int count = (size - startAddrPos) / sizeof(SPOTHERPLAYERS);
		for (int i = 0; i < count; ++i)
		{
			SPOTHERPLAYERS other_PInfo = SPOTHERPLAYERS{};
			memcpy(&other_PInfo, (otherInfo + startAddrPos + sizeof(SPOTHERPLAYERS) * i),
				sizeof(SPOTHERPLAYERS));

			GameObject* other_player = nullptr;

			other_player = m_ObjManager->GetObjFromTag(to_wstring(other_PInfo.id).c_str(),
				OBJ_OTHERPLAYER);

			if (m_curScene == other_PInfo.scene_state &&
				nullptr == other_player)
			{
				TCHAR* tchar = new TCHAR[64];
				wsprintf(tchar, L"%d", other_PInfo.id);

				other_player = AbstractFactory<Player>::CreateObj();
				dynamic_cast<Player*>(other_player)->SetIdToText(other_PInfo.id);
				dynamic_cast<Player*>(other_player)->SetOtherCheck(true);
				m_ObjManager->AddObject(tchar, other_player,
					OBJ_OTHERPLAYER);
			}
		}
	}
	else
	{
		MessageBox(g_hWnd, L"서버에 연결할 수 없습니다.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetwork()
{
	if (false == UpdateNetPlayer())
		return false;
	if (false == UpdateNetOthers())
		return false;
	if (false == UpdateNetMonster())
		return false;
	if (false == UpdateNetEvent())
		return false;
	
	m_NetworkManager->AttackCollisionForNetwork(
		&m_ObjManager->GetObjFromType(OBJ_MONSTER), &m_ObjManager->GetObjFromType(OBJ_EFFECT));

	return true;
}

bool Scene::UpdateNetPlayer()
{
	// 플레이어 정보를 보낸다.
	GameObject* player = m_ObjManager->GetObjFromType(OBJ_PLAYER).begin()->second;

	GAMEOBJINFO objInfo = player->GetInfo();
	char objDir = (char)player->GetDirection();

	PLAYERINFO PInfo = PLAYERINFO{ (short)objInfo.Pos_X, (short)objInfo.Pos_Y,
						player->GetSpriteInfo().CurState, objDir };

	if (false == m_NetworkManager->SendPlayerInfo(m_curScene, PInfo))
	{
		MessageBox(g_hWnd, L"서버에 연결할 수 없습니다.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetOthers()
{
	// 다른 플레이어 정보를 받는다.
	char otherInfo[MAX_BUFFER] = { 0, };
	if (true == m_NetworkManager->SendAndRecvOtherInfo(otherInfo))
	{
		short size = 0;
		memcpy(&size, otherInfo, sizeof(short));
		if (0 == size)
			return true;

		int startAddrPos = 2 + 1;
		if (0 > (size - startAddrPos))
			return true;

		int count = (size - startAddrPos) / sizeof(SPOTHERPLAYERS);
		for (int i = 0; i < count; ++i)
		{
			SPOTHERPLAYERS other_PInfo = SPOTHERPLAYERS{};
			memcpy(&other_PInfo, (otherInfo + startAddrPos + sizeof(SPOTHERPLAYERS) * i),
				sizeof(SPOTHERPLAYERS));

			GameObject* other_player = nullptr;

			other_player = m_ObjManager->GetObjFromTag(to_wstring(other_PInfo.id).c_str(),
				OBJ_OTHERPLAYER);

			if (nullptr != other_player)
			{
				// 위치
				other_player->SetPosition(other_PInfo.info.pos_x, other_PInfo.info.pos_y);

				// 스프라이트 상태
				SPRITEINFO sprite_info = other_player->GetSpriteInfo();
				sprite_info.CurState = other_PInfo.info.player_state;
				other_player->SetSpriteInfo(sprite_info);

				// 방향
				other_player->SetDirection((DIRECTION)other_PInfo.info.player_dir);
			}
		}
	}
	else
	{
		MessageBox(g_hWnd, L"서버에 연결할 수 없습니다.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetMonster()
{
	// 몬스터 정보를 받는다.
	// 씬 정보를 넣는다.
	char otherInfo[MAX_BUFFER] = { 0, };
	otherInfo[3] = m_curScene;
	if (true == m_NetworkManager->SendAndRecvMonster(otherInfo))
	{
		short size = 0;
		memcpy(&size, otherInfo, sizeof(short));
		if (0 == size)
			return true;

		int startAddrPos = 2 + 1;
		if (0 > (size - startAddrPos))
			return true;

		int count = (size - startAddrPos) / sizeof(SPMONSTER);
		for (int i = 0; i < count; ++i)
		{
			SPMONSTER MInfo = SPMONSTER{};
			memcpy(&MInfo, (otherInfo + startAddrPos + sizeof(SPMONSTER) * i),
				sizeof(SPMONSTER));

			GameObject* monster = nullptr;

			monster = m_ObjManager->GetObjFromTag(to_wstring(MInfo.monster_id).c_str(),
				OBJ_MONSTER);

			if (nullptr == monster)
			{
				TCHAR* tchar = new TCHAR[64];
				wsprintf(tchar, L"%d", MInfo.monster_id);

				switch (MInfo.info.monster_type)
				{
				case MONTYPE_SNAIL:
					monster = AbstractFactory<BlueSnail>::CreateObj();
					break;
				case MONTYPE_SLIME:
					monster = AbstractFactory<Slime>::CreateObj();
					break;
				case MONTYPE_BLUEMUSHROOM:
					monster = AbstractFactory<Mushroom>::CreateObj();
					break;
				case MONTYPE_GREENMUSHROOM:
					monster = AbstractFactory<GreenMush>::CreateObj();
					break;
				}

				m_ObjManager->AddObject(tchar, monster,
					OBJ_MONSTER);
			}
			// 위치
			monster->SetPosition(MInfo.info.pos_x, MInfo.info.pos_y);

			// 스프라이트 상태
			SPRITEINFO sprite_info = monster->GetSpriteInfo();
			sprite_info.CurState = MInfo.info.monster_state;
			monster->SetSpriteInfo(sprite_info);

			// 방향
			monster->SetDirection((DIRECTION)MInfo.info.monster_dir);
		}
	}
	else
	{
		MessageBox(g_hWnd, L"서버에 연결할 수 없습니다.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetEvent()
{
	// 이벤트 처리를 한다.
	EVENTINFO evInfo;
	if (true == m_NetworkManager->SendAndRecvEvent(&evInfo))
	{
		switch (evInfo.event_state)
		{
		case EV_PUTOTHERPLAYER:
		{
			if (evInfo.scene_state != m_curScene)
				break;

			GameObject* other_player = nullptr;

			other_player = m_ObjManager->GetObjFromTag(to_wstring(evInfo.id).c_str(),
				OBJ_OTHERPLAYER);

			// 큐로부터 보내온 정보의 다른 유저의 Scene이 현재 Scene과 다른 경우는 
			// 서로 다른 Scene에 있다는 뜻이므로 추가하지 않는다.
			// 하지만 이 정보가 서버의 큐에서 사라지기 때문에 
			// 씬 초기화 할때마다 추가를 해줘야한다.


			// 만약 등록된 id의 플레이어가 없다면 만든다.
			if (nullptr == other_player)
			{
				TCHAR* tchar = new TCHAR[64];
				wsprintf(tchar, L"%d", evInfo.id);

				other_player = AbstractFactory<Player>::CreateObj();
				dynamic_cast<Player*>(other_player)->SetIdToText(evInfo.id);
				dynamic_cast<Player*>(other_player)->SetOtherCheck(true);
				m_ObjManager->AddObject(tchar, other_player,
					OBJ_OTHERPLAYER);
			}
		}
		break;
		case EV_END:
		{
			m_ObjManager->ReleaseObjFromTag(to_wstring(evInfo.id).c_str(),
				OBJ_OTHERPLAYER);
		}
		break;
		case EV_CHAT:
		{
			if (evInfo.scene_state != m_curScene)
				break;

			GameObject* other_player = nullptr;

			other_player = m_ObjManager->GetObjFromTag(to_wstring(evInfo.id).c_str(),
				OBJ_OTHERPLAYER);

			// 채팅을 친 유저의 말이 포함된 풍선을 띄운다.
			if (nullptr != other_player)
			{
				TCHAR tchar[25] = { 0, };
				memcpy(tchar, evInfo.chat_buffer, evInfo.chat_size);
				dynamic_cast<Player*>(other_player)->SetChatBox(tchar);
			}
		}
		break;
		case EV_NONE:
			break;
		}
	}
	else
	{
		MessageBox(g_hWnd, L"서버에 연결할 수 없습니다.", L"Error", MB_OK);
		return false;
	}

	return true;
}
