#include <vector>

#include "raft.pb.h"

namespace Raft {
class Log {
public:
    Log(): 
        _entries(std::vector<Entry>{}),
        _index0(0) {}

    // FIXME: we can pass rvalue reference here
    void mkLog(std::vector<Entry> log, int index0) {
        this->_entries = log;
        this->_index0 = index0;
    }

    // maybe also rvalue reference here?
    void append(const Entry &e) {
        this->_entries.push_back(e);
    }

    inline int start() {
        return this->_index0;
    }

    inline int last() {
        return this->_index0 + this->_entries.size() - 1;
    }

    inline int len() {
        return this->_entries.size();
    }

    Entry lastEntry() {
        return this->_entries.back();
    }

    inline int lastTerm() {
        return this->_entries.back().term();
    }

    inline int getTerm(int index) {
        return this->_entries[index - this->_index0].term();
    }

    void truncateEnd(int index) {
        // resize will truncate the rest part
        this->_entries.resize(index - this->_index0);
    }

    std::vector<Entry> _entries;
    int _index0;
};

}