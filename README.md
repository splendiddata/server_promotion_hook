# server_promotion_hook
Postgres database extension to execute some code when a hot standby server 
promotes to primary server.

The extension creates a transaction callback that activates the user defined
 server\_promotion\_hook.is\_executing\_server\_promotion\_hook() function once in
 the pre\_commit phase of a transaction in every client session when the state
 of a server changes back and forth from in\_hot\_standby to primary.

Unfortunately the pre\_commit phase is too late for the first transaction after
server promotion to know about the change. But it is as good as it gets for
now.

## Postgres versions
The server\_promotion\_hook database extension works well in Postgres versions
15, 16, 17 and 18.

## Installation
First you'll need to compile the database extension (Check the
[Postgres manual](https://www.postgresql.org/docs/current/static/extend-pgxs.html)
for more information):<br>
 - Make sure pg\_config is on your PATH<br>
 - execute: make<br>
 - execute: sudo make install<br>

After compilation, the server\_promotion\_hook.so library must be set to load at session
start. So please alter the postgresql.conf file and add the server\_promotion\_hook.so
library to the session\_preload\_libraries setting. For example:

```
      .
      .
      .

#------------------------------------------------------------------------------
# CUSTOMIZED OPTIONS
#------------------------------------------------------------------------------

# Add settings for extensions here
#
session_preload_libraries = 'server_promotion_hook'
```

Make sure you do that in all involved database servers in your primary /
secundary server setup.
Restart the databases to activate the setting.

**On the primary server**

Then logon to the (primary) database and execute:

```SQL
create extension server_promotion_hook;
```

And create function server\_promotion\_hook.on\_promote\_server(boolean) that
is to be executed when the in\_hot\_standby status changes:

```PLpgSQL
create or replace function server_promotion_hook.on_promote_server(is_primary boolean)
    returns void
	language plpgsql
	as $body$
begin
    if not server_promotion_hook.is_executing_server_promotion_hook() then
	    raise log '% at %:% is explicitly trying to execute server_promotion_hook.on_promote_server(%)', session_user, inet_client_addr(), inet_client_port(), is_primary;
	    raise exception 'server_promotion_hook.on_promote_server(%) is not executed as part of a server promotion action', is_primary;
    end if;
    
    if is_primary then
        // Do the things here that you want to do when the server status
        // has changed from in_hot_standby to primary.
    else
        // There is probably very little that you can do here as the 
        // server status has changed to in_hot_standby. No updates
        // are allowed here!
    end if;
end
$body$;
GRANT EXECUTE ON FUNCTION server_promotion_hook.on_promote_server(boolean) TO PUBLIC;
```

The changes will be propagated to the standby servers automatically

#### Remarks:
the public execute permission is absolutely necessary because the function will
be invoked for everybody / everything that is connected to the database.

Having public access granted to everybody might tempt people to execute the
server\_promotion\_hook.on\_promote\_server(boolean) function at any time. But
of course it is intended to run only when the server status changes.
The server\_promotion\_hook.is\_executing\_server\_promotion\_hook() function
can be used to check if the function is invoked under the control of the
server\_promotion\_hook extension.

## Functions
**server_promotion_hook.is_executing_server_promotion_hook() returns boolean**

    returns true when the server_promotion_hook.on_promote_server(boolean)
    function is invoked under control of the server_promotion_hook code.
    When invoked during a normal session, it will always return false.

**server_promotion_hook.get_login_hook_version() returns text**

    returns the compiled version of the server_promotion_hook software.

**server_promotion_hook.on_promote_server(is_primary boolean) returns void**

    To be provided by you!
