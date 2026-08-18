#pragma once

#include <memory>

namespace features::visuals {

class hitsound_player
{
public:
	hitsound_player( );
	~hitsound_player( );
	hitsound_player( const hitsound_player& ) = delete;
	hitsound_player& operator=( const hitsound_player& ) = delete;

	void play( int style, float volume );

private:
	struct implementation;
	std::unique_ptr<implementation> m_impl;
};

inline hitsound_player& hitsounds( )
{
	static hitsound_player value{};
	return value;
}

}
