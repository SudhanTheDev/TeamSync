#pragma once

#include <string>
#include <utility>

namespace teamsync {

class Person {
public:
    Person() = default;
    Person(int id, std::string fullName, std::string email)
        : id_(id), fullName_(std::move(fullName)), email_(std::move(email)) {}
    virtual ~Person() = default;

    int id() const { return id_; }
    const std::string& fullName() const { return fullName_; }
    const std::string& email() const { return email_; }
    void setFullName(const std::string& value) { fullName_ = value; }
    void setEmail(const std::string& value) { email_ = value; }

    virtual void displayProfile() const = 0;

protected:
    int id_ = 0;
    std::string fullName_;
    std::string email_;
};

} // namespace teamsync
