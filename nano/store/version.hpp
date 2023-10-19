#pragma once

#include <nano/store/component.hpp>

namespace nano
{
class block_hash;
}
namespace nano::store
{
/**
 * Manages version storage
 */
class version
{
public:
	virtual void put (store::write_transaction const &, int) = 0;
	virtual int get (store::transaction const &) const = 0;
};
} // namespace nano::store
