<div align="center">

# TeamSync

### A Local Team Collaboration and Task Management System

<p>
  TeamSync is a C++-based collaboration platform designed to help teams organize projects, assign tasks, communicate, share files, and track progress from one centralized system.
</p>

![C++](https://img.shields.io/badge/C%2B%2B-17%2F20-blue?logo=cplusplus)
![OOP](https://img.shields.io/badge/Concept-Object--Oriented%20Programming-purple)
![Status](https://img.shields.io/badge/Status-In%20Development-orange)
![Platform](https://img.shields.io/badge/Platform-Desktop-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green)

</div>

---

## About the Project

TeamSync is a local team collaboration and task management application developed entirely in C++.

It is designed for student groups, project teams, clubs, and small organizations that need a simple way to manage teamwork without depending on cloud services.

The system allows users to create teams, organize projects, assign tasks, send team messages, share file information, track progress, and generate reports.

This project was developed as a second-semester Object-Oriented Programming project.

---

## Project Details

| Field | Information |
|---|---|
| Project Name | TeamSync |
| Project Type | Local Team Collaboration System |
| Language | C++ |
| Subject | Object-Oriented Programming |
| Semester | Second Semester |
| Instructor | Ojan Adhikari |
| Developers | Sudhan Bhattarai and Severoos Nepali |

---

## Main Features

### User Management

- Register a new user
- Log in and log out
- View and update profile
- Change password
- Support for Admin and Team Member roles
- Unique user identification

### Team Management

- Create a team
- Join a team using a team ID or join code
- View team details
- View team members
- Leave a team
- Remove members with proper permission
- Assign team roles

### Project Management

- Create projects inside teams
- Edit project information
- Set start dates and deadlines
- Track project status
- Archive completed projects
- View project progress

### Task Management

- Create tasks
- Assign tasks to team members
- Set task priority
- Set due dates
- Add descriptions
- Update task progress
- Mark tasks as completed
- Reopen completed tasks
- View pending, completed, and overdue tasks
- Sort and filter tasks

### Team Chat

- Send messages inside a team
- View message history
- Display sender name and date
- Search previous messages
- Store messages locally

### File Sharing

- Share file name and local path
- Add file descriptions
- View shared files
- Open files when available
- Check whether a shared file still exists
- Remove shared file records

### Dashboard

- View joined teams
- View active projects
- View pending tasks
- View completed tasks
- View overdue tasks
- View recent messages
- View team activity
- View task completion statistics

### Reports

- Task completion report
- Pending task report
- Overdue task report
- Project progress report
- Team activity report
- Member contribution report
- Shared files report

---

## Smart Workload Recommendation

TeamSync includes a rule-based task recommendation feature.

The system can recommend a suitable team member for a task based on:

- Current workload
- Active task count
- Completed task count
- Skill match
- Task priority
- Previous completion rate

This feature is not artificial intelligence. It uses a simple and explainable scoring system.

Example:

```text
Task: Design Login Interface
Required Skill: UI Design
Priority: High

Recommended Member: Member 2
Skill Match: 90%
Active Tasks: 2
Recommendation Score: 84/100
```

---

## License

Copyright (c) 2026 Sudhan Bhattarai. All rights reserved.

TeamSync is **source-available, not open source**. You may clone, compile, and
run an unchanged copy solely for personal or educational, non-commercial use.
Modification, derivative works, redistribution, sublicensing, and commercial
use are prohibited without prior written permission. See [LICENSE](LICENSE) for
the complete terms.
