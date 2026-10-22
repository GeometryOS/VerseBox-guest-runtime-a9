package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.Parcelable;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class LocalGuestService {
    static void register(String name, String descriptor, InvocationHandler handler) throws Exception {
        Binder binder = endpoint(descriptor, handler);
        Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                .invoke(null, name, binder);
    }

    static Binder endpoint(String descriptor, InvocationHandler handler) throws Exception {
        Class<?> api = Class.forName(descriptor);
        Map<Integer, Method> transactions = new HashMap<>();
        Class<?> stub = Class.forName(descriptor + "$Stub");
        for (Method method : api.getMethods()) {
            try {
                Field code = stub.getDeclaredField("TRANSACTION_" + method.getName());
                code.setAccessible(true);
                transactions.put(code.getInt(null), method);
            } catch (NoSuchFieldException ignored) { }
        }
        Binder binder = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws android.os.RemoteException {
                Method method = transactions.get(code);
                if (method == null) return super.onTransact(code, data, reply, flags);
                try {
                    data.enforceInterface(descriptor);
                    Class<?>[] types = method.getParameterTypes();
                    Object[] arguments = new Object[types.length];
                    for (int i = 0; i < types.length; ++i) {
                        // IDnsResolver.getResolverInfo has six out arrays, encoded as lengths.
                        if (method.getName().equals("getResolverInfo") && i > 0) {
                            int length = data.readInt();
                            if (length > 65536) throw new IllegalArgumentException("Resolver output size");
                            arguments[i] = length < 0 ? null : Array.newInstance(types[i].getComponentType(), length);
                        } else arguments[i] = read(data, types[i], method.getGenericParameterTypes()[i]);
                    }
                    Object result = handler.invoke(null, method, arguments);
                    if (reply != null && (flags & IBinder.FLAG_ONEWAY) == 0) {
                        reply.writeNoException();
                        write(reply, method.getReturnType(), method.getGenericReturnType(), result);
                        if (method.getName().equals("getResolverInfo"))
                            for (int i = 1; i < types.length; ++i) write(reply, types[i], types[i], arguments[i]);
                    }
                    return true;
                } catch (Throwable error) {
                    if (error instanceof InvocationTargetException) error = error.getCause();
                    if (reply != null && (flags & IBinder.FLAG_ONEWAY) == 0)
                        reply.writeException(error instanceof Exception ? (Exception) error : new IllegalStateException(error));
                    else System.err.println(descriptor + "." + method.getName() + ": " + error);
                    return true;
                }
            }
        };
        IInterface service = (IInterface) Proxy.newProxyInstance(api.getClassLoader(), new Class<?>[] {api},
            (proxy, method, args) -> {
                if ("asBinder".equals(method.getName())) return binder;
                if ("toString".equals(method.getName())) return descriptor;
                if ("hashCode".equals(method.getName())) return System.identityHashCode(proxy);
                if ("equals".equals(method.getName())) return proxy == args[0];
                return handler.invoke(proxy, method, args);
            });
        binder.attachInterface(service, descriptor);
        return binder;
    }

    private static Object read(Parcel data, Class<?> type, Type generic) throws Exception {
        if (type == int.class) return data.readInt();
        if (type == long.class) return data.readLong();
        if (type == boolean.class) return data.readInt() != 0;
        if (type == float.class) return data.readFloat();
        if (type == String.class) return data.readString();
        if (type == byte[].class) return data.createByteArray();
        if (type == int[].class) return data.createIntArray();
        if (type == long[].class) return data.createLongArray();
        if (type == String[].class) return data.createStringArray();
        if (type == List.class) {
            Class<?> item = listItem(generic);
            if (item == String.class) return data.createStringArrayList();
            if (item != null && Parcelable.class.isAssignableFrom(item))
                return data.createTypedArrayList((Parcelable.Creator<?>) item.getField("CREATOR").get(null));
            return data.readArrayList(LocalGuestService.class.getClassLoader());
        }
        if (type == Map.class) return data.readHashMap(LocalGuestService.class.getClassLoader());
        if (IInterface.class.isAssignableFrom(type))
            return Class.forName(type.getName() + "$Stub").getMethod("asInterface", IBinder.class)
                    .invoke(null, data.readStrongBinder());
        if (type == IBinder.class) return data.readStrongBinder();
        if (type.isArray() && Parcelable.class.isAssignableFrom(type.getComponentType()))
            return data.createTypedArray((Parcelable.Creator<?>) type.getComponentType().getField("CREATOR").get(null));
        if (Parcelable.class.isAssignableFrom(type))
            return data.readInt() == 0 ? null : ((Parcelable.Creator<?>) type.getField("CREATOR").get(null)).createFromParcel(data);
        throw new IllegalArgumentException("Guest Binder argument " + type.getName());
    }

    @SuppressWarnings("unchecked")
    private static void write(Parcel reply, Class<?> type, Type generic, Object value) {
        if (type == void.class) return;
        if (type == int.class) reply.writeInt((Integer) value);
        else if (type == long.class) reply.writeLong((Long) value);
        else if (type == boolean.class) reply.writeInt((Boolean) value ? 1 : 0);
        else if (type == String.class) reply.writeString((String) value);
        else if (type == byte[].class) reply.writeByteArray((byte[]) value);
        else if (type == int[].class) reply.writeIntArray((int[]) value);
        else if (type == long[].class) reply.writeLongArray((long[]) value);
        else if (type == String[].class) reply.writeStringArray((String[]) value);
        else if (type == IBinder.class) reply.writeStrongBinder((IBinder) value);
        else if (type == List.class) {
            Class<?> item = listItem(generic);
            if (item == String.class) reply.writeStringList((List<String>) value);
            else if (item != null && Parcelable.class.isAssignableFrom(item))
                reply.writeTypedList((List<? extends Parcelable>) value);
            else reply.writeList((List<?>) value);
        }
        else if (type == Map.class) reply.writeMap((Map<?, ?>) value);
        else if (Parcelable.class.isAssignableFrom(type)) {
            reply.writeInt(value == null ? 0 : 1);
            if (value != null) ((Parcelable) value).writeToParcel(reply, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        } else throw new IllegalArgumentException("Guest Binder result " + type.getName());
    }

    private static Class<?> listItem(Type generic) {
        if (!(generic instanceof ParameterizedType)) return null;
        Type item = ((ParameterizedType) generic).getActualTypeArguments()[0];
        return item instanceof Class<?> ? (Class<?>) item : null;
    }

    static Object call(Object target, String api, String name, Object... arguments) throws Exception {
        for (Method method : Class.forName(api).getMethods())
            if (method.getName().equals(name) && method.getParameterTypes().length == arguments.length)
                return method.invoke(target, arguments);
        throw new NoSuchMethodException(api + "." + name);
    }
}
