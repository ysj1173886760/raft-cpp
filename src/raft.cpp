#include "raft.h"

#include <chrono>
#include <thread>

Raft::Raft() {

}

void Raft::electionThread() {

}

voud Raft::heartbeatThread() {

}

void Raft::commitThread() {
    while (!this->killed()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CommonInterval));

        this->_mu.lock();

        // for snapshot
        if (this->_lastApplied < this->log.start()) {
            this->_lastAppliedt = this->log.start();
        }
        
        while (this->_lastApplied < this->commitIndex) {
            this->_lastApplied++;
            int index = this->_lastApplied;
            CommitMessage msg = CommitMessage(
                this->log._entries[index - this->log.start()].Data,
                index
            );

            // TODO: change this to thread safe queue
            // we may get blocked while pushing the msg
            // we should not hold the mutex while doing some operation
            // that might get blocked
            this->channel.push(msg);
        }

        this->_mu.unlock();
    }
}

void Raft::updateCommitIndexThread() {
    while (!this->killed()) {
    }
}

bool Raft::killed() {
    return this->_dead.load(std::memory_order_relaxed);
}

int main() {

}