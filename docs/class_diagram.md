# Class Diagram

```mermaid
classDiagram
    class Person {
        <<abstract>>
        -int id
        -string fullName
        -string email
        +displayProfile()*
    }
    class User {
        <<abstract>>
        -string username
        -string passwordHash
        -vector~string~ securityAnswerHashes
        -vector~string~ skills
        +role()* UserRole
        +canManageTeams()* bool
    }
    class Admin
    class Member
    Person <|-- User
    User <|-- Admin
    User <|-- Member

    class Team {
        -vector~int~ memberIds
        -vector~int~ adminIds
        +addMember()
        +removeMember()
        +addAdmin()
    }
    class Project {
        -int teamId
        -vector~int~ taskIds
        +setDates()
        +setStatus()
    }
    class Task {
        <<abstract>>
        -TaskPriority priority
        -TaskStatus status
        -string completionNote
        +start()
        +markCompleted()
        +type()* TaskType
        +displayDetails()
        +calculateWeight()
    }
    class GeneralTask
    class DevelopmentTask
    class ResearchTask
    class DocumentationTask
    Task <|-- GeneralTask
    Task <|-- DevelopmentTask
    Task <|-- ResearchTask
    Task <|-- DocumentationTask

    Team "many" o-- "many" User : member IDs
    Team "1" o-- "many" Project : team ID
    Project "1" o-- "many" Task : task IDs

    class Message
    class ChatRoom {
        -vector~Message~ messages
    }
    ChatRoom *-- Message
    Team "1" o-- "many" Message

    class SharedFile
    class TaskAttachment
    class Notification
    class ActivityLog
    Team "1" o-- "many" SharedFile
    Task "1" o-- "many" TaskAttachment
    User "1" o-- "many" Notification
    User "1" o-- "many" ActivityLog

    class Authentication
    class FileManager
    class Dashboard
    class ReportGenerator
    class TeamSyncService
    class TeamSyncApplication
    class LanSyncManager
    TeamSyncApplication --> TeamSyncService
    TeamSyncService *-- Authentication
    TeamSyncService *-- FileManager
    TeamSyncService *-- User
    TeamSyncService *-- Team
    TeamSyncService *-- Project
    TeamSyncService *-- Task
    Dashboard --> TeamSyncService
    ReportGenerator --> TeamSyncService
    LanSyncManager --> FileManager : synchronizes workspace files
```

An open diamond means aggregation by stable IDs; a filled diamond means direct object ownership. This avoids unsafe owning pointers between records while still expressing the real domain relationships.
