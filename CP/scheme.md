```
            ┌──────────────────────────────┐
            │           CLIENT N           │
            │      (процесс игрока)        │
            │                              │
            │   - ввод команд              │
            │   - отрисовка полей          │
            │   - отправка запросов        │
            │   - ожидание ответов         │
            └───────────────┬──────────────┘
                            │
                            │ Request[N] / Response[N]
                            │ через Shared Memory
                            ▼
┌──────────────────────────────────────────────────────────┐
│                        SHARED MEMORY                     │
│                     (mmap + shm_open)                    │
│                                                          │
│  ┌──────────────┐    ┌──────────────┐    ┌────────────┐  │
│  │ players[ ]   │    │ games[ ]     │    │ invites[ ] │  │
│  │ - active     │    │ - active     │    │ - active   │  │
│  │ - login      │    │ - players[2] │    │ - from/to  │  │
│  │ - game_id    │    │ - turn       │    │ - game_id  │  │
│  └──────────────┘    │ - ships      │    └────────────┘  │
│                      │ - boards     │                    │
│                      └──────────────┘                    │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ requests[MAX_PLAYERS]                              │  │
│  │   - type                                           │  │
│  │   - from_slot                                      │  │
│  │   - args (x,y)                                     │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ responses[MAX_PLAYERS]                             │  │
│  │   - code / text / boards                           │  │
│  │   - winner                                         │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  request_ready[MAX_PLAYERS]                              │
└───────────────────────────┬──────────────────────────────┘
                            │
                            │ Semaphore Signals
                            ▼
            ┌──────────────────────────────────┐
            │              SERVER              │
            │      (центральный процесс)       │
            │----------------------------------│
            │   server_loop():                 │
            │   - ждёт request_sem             │
            │   - ищет первый request_ready    │
            │   - process_request(slot)        │
            │----------------------------------│
            │   process_request():             │
            │    - switch(req.type)            │
            │    - обновление Game/Player      │
            │    - запись ответа               │
            │    - sem_post(resp_sems[slot])   │
            └───────────────┬──────────────────┘
                            │
                            │ slot_sems[i]
                            │ для защиты слота i
                            ▼
             ┌───────────────────────────────────┐
             │        Семафоры (POSIX)           │
             │-----------------------------------│
             │ request_sem (client → server)     │
             │ resp_sems[i] (server → client)    │
             │ slot_sems[i] (мьютекс слота)      │
             └───────────────────────────────────┘
```

```mermaid
graph TB
    subgraph "Client Process"
        Client[client.c]
        ClientIPC[IPC Client Interface]
        ClientUI[User Interface<br/>Menu & Input]
    end

    subgraph "Shared Memory (IPC)"
        SHM[SharedState]
        
        subgraph "Data Structures"
            Players[Players Array<br/>MAX_PLAYERS=10]
            Games[Games Array<br/>MAX_GAMES=20]
            Invites[Invites Array<br/>MAX_INVITES=20]
            Requests[Request Queue<br/>per player slot]
            Responses[Response Queue<br/>per player slot]
        end
        
        subgraph "Synchronization"
            ReqSem[Request Semaphore]
            SlotSems[Slot Semaphores<br/>per player]
            RespSems[Response Semaphores<br/>per player]
        end
    end

    subgraph "Server Process"
        Server[server.c]
        ServerIPC[IPC Server Interface]
        
        subgraph "Request Handlers"
            RegHandler[Register Handler]
            GameHandler[Game Management<br/>Create/Join/List]
            InviteHandler[Invite System<br/>Send/Accept/Decline]
            GameplayHandler[Gameplay Logic<br/>Fire/Status/Quit]
            LogoutHandler[Logout Handler]
        end
        
        subgraph "Game Logic"
            ShipPlacement[Ship Placement<br/>5 ships: 4,3,3,2,2]
            BoardMgmt[Board Management<br/>8x8 grid]
            TurnLogic[Turn Management]
            WinCheck[Win Condition Check]
        end
    end

    subgraph "Common Definitions"
        CommonH[common.h]
        
        subgraph "Types"
            ReqTypes[Request Types<br/>13 types]
            CellStates[Cell States<br/>Empty/Ship/Miss/Hit]
            Structs[Data Structures<br/>Player/Game/Invite]
        end
    end

    Client --> ClientIPC
    ClientIPC --> SHM
    
    ClientUI -.->|1. Login| Client
    ClientUI -.->|2. Create/Join Game| Client
    ClientUI -.->|3. Send/Accept Invite| Client
    ClientUI -.->|4. Fire Shot| Client
    ClientUI -.->|5. View Status| Client
    
    SHM --> ServerIPC
    ServerIPC --> Server
    
    Server --> RegHandler
    Server --> GameHandler
    Server --> InviteHandler
    Server --> GameplayHandler
    Server --> LogoutHandler
    
    GameplayHandler --> ShipPlacement
    GameplayHandler --> BoardMgmt
    GameplayHandler --> TurnLogic
    GameplayHandler --> WinCheck
    
    CommonH -.->|defines| Client
    CommonH -.->|defines| Server
    CommonH -.->|defines| SHM
    
    ReqSem -.->|signals| Server
    SlotSems -.->|protects| Requests
    RespSems -.->|signals| Client

    style SHM fill:#4fc3f7
    style Client fill:#ffb74d
    style Server fill:#81c784
    style CommonH fill:#ba68c8
```
