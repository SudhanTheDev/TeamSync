# TeamSync Project Proposal

## Academic information

- Students: Sudhan Bhattarai and Severoos Nepali
- Semester: Second Semester
- Subject: Object-Oriented Programming
- Instructor: Ojan Adhikari
- Project Support: Senior Sudip Khanal

## Problem

Small student teams often divide work through scattered messages and memory. Members cannot easily see who owns a task, when work is due, what has been completed, or where a shared local file is stored. Online collaboration services may be unnecessary or unavailable for a classroom OOP demonstration.

## Proposed solution

TeamSync is a C++17 console application that keeps users, teams, projects, tasks, messages, shared-file paths, notifications, activities, and reports together on one computer. It demonstrates OOP through real domain behavior instead of isolated examples.

## Objectives

1. Provide reliable registration and local login.
2. Model users, teams, projects, and polymorphic tasks with clear relationships.
3. Enforce team membership and administrative permissions.
4. Preserve all important records between sessions.
5. Provide explainable workload and contribution estimates.
6. Produce readable on-screen and text-file reports.
7. Remain small enough for both students to explain during a viva.

## Scope

The original proposal targeted one computer. The delivered extension adds trusted-LAN GUI host/join synchronization, including copied task-completion files and workspace-owned shared files. It still excludes internet/cloud synchronization, internet-grade encryption, AI, voice control, and instant push delivery.

## Tools and technology

- C++17 and the Standard Library
- CMake 3.16+
- Text-delimited local files
- STL smart pointers, containers, algorithms, streams, and filesystem

## Expected result

A compilable and tested program in which a user can complete the full workflow: register, log in, create or join a team, create a project and task, assign/update work, communicate, share a file path, read notifications, inspect a dashboard, and export reports.
