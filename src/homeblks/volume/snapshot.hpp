/*
 * Copyright eBay Inc, 2019
 */

#pragma once
#include <sds_logging/logging.h>

namespace homestore {

class Snapshot {
private:
    uint64_t m_snapId;
    uint64_t m_seqId;
    boost::uuids::uuid m_uuid;

    Volume* m_volume;

public:
    Snapshot(Volume* vol, uint64_t snapId, uint64_t seqId) : m_snapId(snapId), m_seqId(seqId), m_volume(vol){};

    ~Snapshot(){};

    std::string to_string() {
        std::stringstream ss;
        ss << "Snapshot: snapId:" << m_snapId << ", SeqId:" << m_seqId << ", Volume[" << m_volume << "]";
        return ss.str();
    }
};

} // namespace homestore

typedef std::shared_ptr< Snapshot > SnapshotPtr;