#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

class Chat 
{
private:

    using memory_object_t       = boost::interprocess::shared_memory_object;
    using shared_memory_t       = boost::interprocess::managed_shared_memory;
    using manager_t             = shared_memory_t::segment_manager;
    using string_allocator_t    = boost::interprocess::allocator< char, manager_t >;
    using string_t              = boost::interprocess::basic_string< char, std::char_traits< char >, string_allocator_t>;
    using vector_allocator_t    = boost::interprocess::allocator< string_t, manager_t >;
    using vector_t              = boost::interprocess::vector < string_t, vector_allocator_t >;
    using mutex_t               = boost::interprocess::interprocess_mutex;
    using condition_t           = boost::interprocess::interprocess_condition;
    using counter_t             = std::atomic < std::size_t >;

private:

    static inline const auto shared_memory_name      = "shared_memory";
    static inline const auto mutex_name              = "mutex";
    static inline const auto condition_name          = "condition";
    static inline const auto vector_name             = "vector";
    static inline const auto user_counter_name       = "number_of_users";
    static inline const auto message_counter_name    = "message_ID";
    static inline const auto memory_size             = 65536ULL;

private:
    std::string     m_user_name;

    std::atomic < bool > m_exit_flag;

    std::size_t     m_local_messages;

    shared_memory_t m_shared_memory;

    vector_t*       m_vector;
    mutex_t*        m_mutex;
    condition_t*    m_condition;
    counter_t*      m_users;
    counter_t*      m_messages;


public:

    explicit Chat(const std::string& user_name) : m_user_name(user_name), m_exit_flag(false), m_local_messages(0U),
        m_shared_memory(shared_memory_t(boost::interprocess::open_or_create, shared_memory_name, memory_size))
    {
        m_vector    = m_shared_memory.find_or_construct<vector_t>(vector_name)          (m_shared_memory.get_segment_manager());
        m_mutex     = m_shared_memory.find_or_construct<mutex_t>(mutex_name)            ();
        m_condition = m_shared_memory.find_or_construct<condition_t>(condition_name)    ();
        m_users     = m_shared_memory.find_or_construct<counter_t>(user_counter_name)   (0ULL);
        m_messages  = m_shared_memory.find_or_construct<counter_t>(message_counter_name)();

        ++(*m_users);
    }

    ~Chat() noexcept = default;

public:

    void run() 
    {
        auto reader = std::thread(&Chat::read, this);

        write();

        reader.join();

        send_message(m_user_name + " left the chat");

        if (!(--(*m_users)))
        	memory_object_t::remove(shared_memory_name);
    }

private:

    void read()
    {
        send_message(m_user_name + " joined the chat");

        while (true)
        {
            std::unique_lock lock(*m_mutex);

            m_condition->wait(lock, [this]() { return !m_vector->empty(); });

            if (m_exit_flag)
                break;
            
            while (std::size(*m_vector) != m_local_messages)
            {
                std::cout << (*m_vector)[m_local_messages] << std::endl;
                ++m_local_messages;
            }
        }
    }

    void send_message(const std::string & message)
    {
        boost::interprocess::scoped_lock lock(*m_mutex);

        m_vector->push_back(string_t(message.c_str(), m_shared_memory.get_segment_manager()));

        m_condition->notify_all();
    }

    void write()
    {
        std::string message;

        while(!m_exit_flag)
        {
            std::getline(std::cin, message);

            if (message == "!exit")
                m_exit_flag = true;
            else
	            send_message('[' + m_user_name + "]: " + message);
            
        }
    }
};

int main(int argc, char ** argv) 
{
    std::string user_name;

    boost::interprocess::shared_memory_object::remove("shared_memory");

    std::cout << "Enter your name: ";

    std::getline(std::cin, user_name);

    std::cout << "Type \"!exit\" to close chat session" << std::endl;

    Chat(user_name).run();

    system("pause");

    return EXIT_SUCCESS;
}