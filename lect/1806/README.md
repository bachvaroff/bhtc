sem_take
lock:
	lock_mutex
	if val == 0 then begin
		unlock_mutex
		goto lock
	end
	val--
	unlock mutex

sem_give
	lock_mutex
	val++
	if val > max then begin
		val = max
	end
	unlock_mutex
