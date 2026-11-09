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

	// ť�κ��� ���� ���ߴ� �ٸ� �÷��̾� ������ �޴´�.
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
		MessageBox(g_hWnd, L"������ ������ �� �����ϴ�.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetwork()
{
	// LAN 부하 감소: 네트워크는 매 프레임이 아닌 20Hz로만 수행
	static LARGE_INTEGER lastNet;
	static bool firstNet = true;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (firstNet)
	{
		lastNet = now;
		firstNet = false;
	}
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	if ((now.QuadPart - lastNet.QuadPart) * 1000 < freq.QuadPart * 50)
		return true;
	lastNet = now;

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
	// �÷��̾� ������ ������.
	GameObject* player = m_ObjManager->GetObjFromType(OBJ_PLAYER).begin()->second;

	GAMEOBJINFO objInfo = player->GetInfo();
	char objDir = (char)player->GetDirection();

	PLAYERINFO PInfo = PLAYERINFO{ (short)objInfo.Pos_X, (short)objInfo.Pos_Y,
						player->GetSpriteInfo().CurState, objDir };

	if (false == m_NetworkManager->SendPlayerInfo(m_curScene, PInfo))
	{
		MessageBox(g_hWnd, L"������ ������ �� �����ϴ�.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetOthers()
{
	// �ٸ� �÷��̾� ������ �޴´�.
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
				// ��ġ
				other_player->SetPosition(other_PInfo.info.pos_x, other_PInfo.info.pos_y);

				// ��������Ʈ ����
				SPRITEINFO sprite_info = other_player->GetSpriteInfo();
				sprite_info.CurState = other_PInfo.info.player_state;
				other_player->SetSpriteInfo(sprite_info);

				// ����
				other_player->SetDirection((DIRECTION)other_PInfo.info.player_dir);
			}
		}
	}
	else
	{
		MessageBox(g_hWnd, L"������ ������ �� �����ϴ�.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetMonster()
{
	// ���� ������ �޴´�.
	// �� ������ �ִ´�.
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
			// ��ġ
			monster->SetPosition(MInfo.info.pos_x, MInfo.info.pos_y);

			// ��������Ʈ ����
			SPRITEINFO sprite_info = monster->GetSpriteInfo();
			sprite_info.CurState = MInfo.info.monster_state;
			monster->SetSpriteInfo(sprite_info);

			// ����
			monster->SetDirection((DIRECTION)MInfo.info.monster_dir);
		}
	}
	else
	{
		MessageBox(g_hWnd, L"������ ������ �� �����ϴ�.", L"Error", MB_OK);
		return false;
	}

	return true;
}

bool Scene::UpdateNetEvent()
{
	// �̺�Ʈ ó���� �Ѵ�.
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

			// ť�κ��� ������ ������ �ٸ� ������ Scene�� ���� Scene�� �ٸ� ���� 
			// ���� �ٸ� Scene�� �ִٴ� ���̹Ƿ� �߰����� �ʴ´�.
			// ������ �� ������ ������ ť���� ������� ������ 
			// �� �ʱ�ȭ �Ҷ����� �߰��� ������Ѵ�.


			// ���� ��ϵ� id�� �÷��̾ ���ٸ� �����.
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

			// ä���� ģ ������ ���� ���Ե� ǳ���� ����.
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
		MessageBox(g_hWnd, L"������ ������ �� �����ϴ�.", L"Error", MB_OK);
		return false;
	}

	return true;
}
