/*
 * ===============================================================
 *    Description:  Each graph element (node or edge) can have
 *                  properties, which are key-value pairs 
 *
 *        Created:  Friday 12 October 2012 01:28:02  EDT
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#ifndef weaver_db_property_h_
#define weaver_db_property_h_

#include <string>
#include <functional>

#include "common/weaver_constants.h"
#include "common/vclock.h"

#include "node_prog/property.h"

namespace db
{
    class property : public node_prog::property
    {
        public:
            property();
            property(const std::string&, const std::string&);
            property(const std::string&, const std::string&, const vc::vclock&);
            property(const property &other);

            vc::vclock creat_time;
            vc::vclock del_time;

            bool operator==(property const &p2) const;

            const vc::vclock& get_creat_time() const;
            const vc::vclock& get_del_time() const;
            void update_del_time(const vc::vclock&);
    };

    class property_key_hasher
    {
        size_t operator() (const property &p) const;
    };
}

#endif
