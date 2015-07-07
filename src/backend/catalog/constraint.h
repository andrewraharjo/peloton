/*-------------------------------------------------------------------------
 *
 * constraint.h
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/catalog/constraint.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "backend/common/types.h"


#include <string>
#include <vector>
#include <iostream>

namespace peloton {

namespace index {
class Index;
}
namespace storage {
class DataTable;
}

namespace catalog {

/**
 * Constraint Catalog Object
 */
class Constraint
{

public:
    Constraint( ConstraintType type, std::string name = "", std::string reference_table_name = "" )
                : type(type), name(name), reference_table_name(reference_table_name) {}
 
    
    //===--------------------------------------------------------------------===//
    // ACCESSORS
    //===--------------------------------------------------------------------===//

    ConstraintType GetType() const {
        return type;
    }

    std::string GetName() const {
        return name;
    }

    std::string GetReferenceTableName() const {
        return reference_table_name;
    }

    // Get a string representation of this constraint
    friend std::ostream& operator<<(std::ostream& os, const Constraint& constraint);

private:
    
    //===--------------------------------------------------------------------===//
    // MEMBERS
    //===--------------------------------------------------------------------===//
    
    // The type of constraint
    ConstraintType type = CONSTRAINT_TYPE_INVALID;

    // Constraint name (if needed)
    std::string name = "";

    // reference table names
    std::string reference_table_name = "";

};

} // End catalog namespace
} // End peloton namespace
