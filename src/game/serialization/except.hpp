#pragma once

namespace ser
{

struct archive_check_error : std::runtime_error
{
	archive_check_error(std::string const& msg)
	: std::runtime_error(msg)
	{
	}
};


}
